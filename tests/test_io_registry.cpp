// The IO extension point. No format handlers ship until M5, so what is under
// test here is the registry itself: dispatch, ownership and the errors a host
// sees when it asks for a format nobody registered.

#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using namespace cadgeom;

namespace {

struct IoLedger {
    int imported = 0;
    int exported = 0;
    int importersReleased = 0;
    int exportersReleased = 0;
    std::string lastPath;
};

class FakeImporter final : public IImporter {
public:
    explicit FakeImporter(IoLedger& ledger) : ledger_(ledger) {}

    void Release() override {
        ++ledger_.importersReleased;
        delete this;
    }

    const char* GetName() const override { return "Fake"; }

    CgResult Import(const char* path, IScene* scene, const ImportOptions&, IoProgressCallback,
                    void*) override {
        ++ledger_.imported;
        ledger_.lastPath = path ? path : "";
        // Prove the registry hands over a usable scene, not a null placeholder.
        return scene && IsValid(scene->CreateGroup("Imported", kInvalidEntity))
                   ? CgResult::Ok
                   : CgResult::Unknown;
    }

private:
    ~FakeImporter() override = default;
    IoLedger& ledger_;
};

class FakeExporter final : public IExporter {
public:
    explicit FakeExporter(IoLedger& ledger) : ledger_(ledger) {}

    void Release() override {
        ++ledger_.exportersReleased;
        delete this;
    }

    const char* GetName() const override { return "Fake"; }

    CgResult Export(const char* path, const IScene* scene, const ExportOptions&,
                    IoProgressCallback, void*) override {
        ++ledger_.exported;
        ledger_.lastPath = path ? path : "";
        return scene ? CgResult::Ok : CgResult::Unknown;
    }

private:
    ~FakeExporter() override = default;
    IoLedger& ledger_;
};

} // namespace

TEST_CASE("nothing is registered before M5", "[io]") {
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    CHECK(io.GetFormatCount() == 0);
    CHECK(io.GetFormatExtension(0) == nullptr);
    CHECK_FALSE(io.CanImport("obj"));
    CHECK_FALSE(io.CanExport("gltf"));

    ExportOptions options{};
    CHECK(io.Export("model.glb", options, nullptr, nullptr) == CgResult::NotSupported);
    // The message has to name the milestone, or this looks like a bug.
    CHECK(std::strstr(fixture.Engine().GetLastErrorMessage(), "M5") != nullptr);
}

TEST_CASE("a host can register its own format today", "[io]") {
    IoLedger ledger;
    {
        cgtest::EngineFixture fixture;
        IIoRegistry& io = *fixture.Engine().GetIoRegistry();

        REQUIRE(CgSucceeded(
            io.Register("step", new FakeImporter(ledger), new FakeExporter(ledger))));

        CHECK(io.GetFormatCount() == 1);
        CHECK(std::string(io.GetFormatExtension(0)) == "step");
        CHECK(io.CanImport("step"));
        CHECK(io.CanExport("step"));

        ImportOptions importOptions{};
        REQUIRE(CgSucceeded(io.Import("part.step", importOptions, nullptr, nullptr)));
        CHECK(ledger.imported == 1);
        CHECK(ledger.lastPath == "part.step");
        CHECK(fixture.Scene().GetEntityCount() == 1);

        ExportOptions exportOptions{};
        REQUIRE(CgSucceeded(io.Export("out.step", exportOptions, nullptr, nullptr)));
        CHECK(ledger.exported == 1);
    }

    // The registry owns what it was given and frees it at shutdown.
    CHECK(ledger.importersReleased == 1);
    CHECK(ledger.exportersReleased == 1);
}

TEST_CASE("extensions are matched case-insensitively and dot-insensitively", "[io]") {
    IoLedger ledger;
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    REQUIRE(CgSucceeded(io.Register(".STEP", new FakeImporter(ledger), nullptr)));
    CHECK(io.CanImport("step"));
    CHECK(io.CanImport("STEP"));
    CHECK(io.CanImport(".Step"));

    ImportOptions options{};
    CHECK(CgSucceeded(io.Import("C:/models/PART.STEP", options, nullptr, nullptr)));
}

TEST_CASE("dispatch prefers an explicit format over the file extension", "[io]") {
    IoLedger ledger;
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    REQUIRE(CgSucceeded(io.Register("obj", new FakeImporter(ledger), nullptr)));

    ImportOptions options{};
    options.format = FileFormat::Obj;
    // The caller knows what the bytes are; the extension is only a fallback.
    CHECK(CgSucceeded(io.Import("mesh.dat", options, nullptr, nullptr)));
    CHECK(ledger.imported == 1);
}

TEST_CASE("a path with no usable extension is refused", "[io]") {
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    ImportOptions options{};
    CHECK(io.Import("model", options, nullptr, nullptr) == CgResult::InvalidArgument);
    CHECK(io.Import("", options, nullptr, nullptr) == CgResult::InvalidArgument);

    // A dot in a directory name is not an extension.
    CHECK(io.Import("C:/my.models/part", options, nullptr, nullptr) ==
          CgResult::InvalidArgument);
}

TEST_CASE("registering over a format replaces only the direction given", "[io]") {
    IoLedger ledger;
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    REQUIRE(CgSucceeded(io.Register("obj", new FakeImporter(ledger), new FakeExporter(ledger))));
    REQUIRE(CgSucceeded(io.Register("obj", new FakeImporter(ledger), nullptr)));

    CHECK(ledger.importersReleased == 1);  // The superseded importer.
    CHECK(ledger.exportersReleased == 0);  // The exporter was never replaced.
    CHECK(io.CanExport("obj"));
    CHECK(io.GetFormatCount() == 1);
}

TEST_CASE("unregistering releases the handlers", "[io]") {
    IoLedger ledger;
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    REQUIRE(CgSucceeded(io.Register("obj", new FakeImporter(ledger), new FakeExporter(ledger))));
    REQUIRE(CgSucceeded(io.Unregister("obj")));

    CHECK(ledger.importersReleased == 1);
    CHECK(ledger.exportersReleased == 1);
    CHECK(io.GetFormatCount() == 0);
    CHECK_FALSE(io.CanImport("obj"));
    CHECK(io.Unregister("obj") == CgResult::NotFound);
}

TEST_CASE("registration is rejected when there is nothing to register", "[io]") {
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    CHECK(io.Register("obj", nullptr, nullptr) == CgResult::InvalidArgument);
    CHECK(io.Register("", nullptr, nullptr) == CgResult::InvalidArgument);
    CHECK(io.Register(nullptr, nullptr, nullptr) == CgResult::InvalidArgument);
    CHECK(io.GetFormatCount() == 0);
}

TEST_CASE("import-only and export-only formats dispatch independently", "[io]") {
    IoLedger ledger;
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    REQUIRE(CgSucceeded(io.Register("in", new FakeImporter(ledger), nullptr)));

    ExportOptions options{};
    CHECK(io.Export("file.in", options, nullptr, nullptr) == CgResult::NotSupported);
    CHECK_FALSE(io.CanExport("in"));
    CHECK(io.CanImport("in"));
}

TEST_CASE("the tool manager owns registered tools", "[tools]") {
    struct ToolLedger {
        int released = 0;
        int activated = 0;
        int deactivated = 0;
    } ledger;

    class NoopTool final : public ITool {
    public:
        explicit NoopTool(ToolLedger& ledger) : ledger_(ledger) {}
        void Release() override {
            ++ledger_.released;
            delete this;
        }
        ToolId GetId() const override { return ToolId::Custom; }
        const char* GetName() const override { return "Noop"; }
        void OnActivate(IToolContext*) override { ++ledger_.activated; }
        void OnDeactivate(IToolContext*) override { ++ledger_.deactivated; }
        ToolResult OnMouseDown(const MouseEvent&, IToolContext*) override {
            return ToolResult::Ignored;
        }
        ToolResult OnMouseMove(const MouseEvent&, IToolContext*) override {
            return ToolResult::Ignored;
        }
        ToolResult OnMouseUp(const MouseEvent&, IToolContext*) override {
            return ToolResult::Ignored;
        }
        ToolResult OnKey(const KeyEvent&, IToolContext*) override {
            return ToolResult::Ignored;
        }
        void BuildPreview(IOverlayBuilder*, IToolContext*) override {}
        void OnCancel(IToolContext*) override {}

    private:
        ~NoopTool() override = default;
        ToolLedger& ledger_;
    };

    {
        cgtest::EngineFixture fixture;
        IToolManager& tools = *fixture.Engine().GetToolManager();

        CHECK(tools.GetActiveTool() == ToolId::None);
        // The built-in tools are M2 work; asking for one says so.
        CHECK(tools.Activate(ToolId::Circle) == CgResult::NotImplemented);

        REQUIRE(CgSucceeded(tools.RegisterTool(new NoopTool(ledger))));
        REQUIRE(CgSucceeded(tools.Activate(ToolId::Custom)));
        CHECK(tools.GetActiveTool() == ToolId::Custom);
        CHECK(tools.GetActiveToolInterface() != nullptr);

        CHECK(tools.RegisterTool(nullptr) == CgResult::InvalidArgument);
        CHECK(tools.UnregisterTool(ToolId::Line) == CgResult::NotFound);

        tools.SetSnapMask(Snap_Grid);
        CHECK(tools.GetSnapMask() == Snap_Grid);
        tools.SetContinuousMode(true);
        CHECK(tools.IsContinuousMode());
    }

    CHECK(ledger.released == 1);
}
