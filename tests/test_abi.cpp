// The ABI contract from docs/architecture.md §2.2 — the rules that turn a
// mysterious crash in someone else's process into a clear error at startup.

#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

using namespace cadgeom;

TEST_CASE("the loaded library reports the version its headers declare", "[abi]") {
    CHECK(CadGeom_GetApiVersion() == CADGEOM_API_VERSION);
    CHECK(CadGeom_IsApiVersionCompatible(CADGEOM_API_VERSION) != 0);
    CHECK(CadGeom_GetBuildInfo() != nullptr);
    CHECK(std::strlen(CadGeom_GetBuildInfo()) > 0);
}

TEST_CASE("version packing round-trips", "[abi]") {
    const uint32_t v = CADGEOM_MAKE_VERSION(3, 17, 254);
    CHECK(CADGEOM_VERSION_MAJOR(v) == 3);
    CHECK(CADGEOM_VERSION_MINOR(v) == 17);
    CHECK(CADGEOM_VERSION_PATCH(v) == 254);
}

TEST_CASE("compatibility follows the append-only rule", "[abi]") {
    constexpr uint32_t kMajor = CADGEOM_API_VERSION_MAJOR;
    constexpr uint32_t kMinor = CADGEOM_API_VERSION_MINOR;

    SECTION("a different major is always rejected") {
        CHECK(CadGeom_IsApiVersionCompatible(CADGEOM_MAKE_VERSION(kMajor + 1, kMinor, 0)) == 0);
    }

    SECTION("a host asking for a newer minor is rejected") {
        // It would call vtable slots this binary does not have.
        CHECK(CadGeom_IsApiVersionCompatible(CADGEOM_MAKE_VERSION(kMajor, kMinor + 1, 0)) == 0);
    }

    SECTION("an older minor is fine") {
        // Appending never moves an existing slot, so an older host still lines up.
        CHECK(CadGeom_IsApiVersionCompatible(CADGEOM_MAKE_VERSION(kMajor, 0, 0)) != 0);
    }

    SECTION("the patch level is irrelevant") {
        CHECK(CadGeom_IsApiVersionCompatible(CADGEOM_MAKE_VERSION(kMajor, kMinor, 999)) != 0);
    }
}

TEST_CASE("engine creation refuses a mismatched host", "[abi]") {
    EngineDesc desc{};
    desc.apiVersion = CADGEOM_MAKE_VERSION(CADGEOM_API_VERSION_MAJOR + 1, 0, 0);
    desc.logLevel = LogLevel::Off;

    ICadEngine* engine = CadGeom_CreateEngine(desc);
    REQUIRE(engine == nullptr);

    // A refusal has to say why; that is the entire point of the check.
    const char* message = CadGeom_GetCreateEngineError();
    REQUIRE(message != nullptr);
    CHECK(std::strstr(message, "version") != nullptr);
}

TEST_CASE("an unavailable kernel is refused rather than substituted", "[abi]") {
    EngineDesc desc{};
    desc.kernelType = KernelType::Occt;
    desc.logLevel = LogLevel::Off;

    CHECK(CadGeom_CreateEngine(desc) == nullptr);
    CHECK(std::strstr(CadGeom_GetCreateEngineError(), "Occt") != nullptr);
}

TEST_CASE("create/release leaves nothing behind", "[abi][lifetime]") {
    const uint64_t baseline = CadGeom_GetLiveObjectCount();

    SECTION("one engine") {
        EngineDesc desc{};
        desc.logLevel = LogLevel::Off;
        ICadEngine* engine = CadGeom_CreateEngine(desc);
        REQUIRE(engine != nullptr);
        CHECK(CadGeom_GetLiveObjectCount() > baseline);
        engine->Release();
    }

    SECTION("several at once") {
        EngineDesc desc{};
        desc.logLevel = LogLevel::Off;
        ICadEngine* a = CadGeom_CreateEngine(desc);
        ICadEngine* b = CadGeom_CreateEngine(desc);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(a->GetScene() != b->GetScene());
        b->Release();
        a->Release();
    }

    SECTION("repeatedly") {
        for (int i = 0; i < 16; ++i) {
            EngineDesc desc{};
            desc.logLevel = LogLevel::Off;
            ICadEngine* engine = CadGeom_CreateEngine(desc);
            REQUIRE(engine != nullptr);
            engine->GetScene()->CreateGroup("scratch", kInvalidEntity);
            engine->Release();
        }
    }

    CHECK(CadGeom_GetLiveObjectCount() == baseline);
}

TEST_CASE("the engine exposes every sub-system", "[abi]") {
    cgtest::EngineFixture fixture;
    ICadEngine& engine = fixture.Engine();

    CHECK(engine.GetScene() != nullptr);
    CHECK(engine.GetToolManager() != nullptr);
    CHECK(engine.GetIoRegistry() != nullptr);
    CHECK(engine.GetDeviceName() != nullptr);
    CHECK(engine.GetViewportCount() == 0);
    CHECK(engine.GetViewportAt(0) == nullptr);
    CHECK(engine.GetExtension(0) == nullptr);

    // Sub-systems are stable objects, not freshly minted proxies.
    CHECK(engine.GetScene() == engine.GetScene());
    CHECK(engine.GetToolManager() == engine.GetToolManager());
}

TEST_CASE("failures report through the error channel, never by throwing", "[abi]") {
    cgtest::EngineFixture fixture;
    ICadEngine& engine = fixture.Engine();

    engine.ClearLastError();
    CHECK(engine.GetLastError() == CgResult::Ok);

    ViewportDesc desc{};
    CHECK(engine.CreateViewport(desc) == nullptr);
    CHECK(engine.GetLastError() == CgResult::NotImplemented);
    REQUIRE(engine.GetLastErrorMessage() != nullptr);
    CHECK(std::strlen(engine.GetLastErrorMessage()) > 0);

    engine.ClearLastError();
    CHECK(engine.GetLastError() == CgResult::Ok);
    CHECK(std::strlen(engine.GetLastErrorMessage()) == 0);
}

TEST_CASE("Tick survives a hostile delta", "[abi]") {
    cgtest::EngineFixture fixture;

    // A host that stalls on a breakpoint or divides by a zero frame count can
    // hand us anything; none of it may escape into the engine's state.
    fixture.Engine().Tick(0.016);
    fixture.Engine().Tick(0.0);
    fixture.Engine().Tick(-1.0);
    fixture.Engine().Tick(1e9);
    fixture.Engine().Tick(std::numeric_limits<double>::quiet_NaN());
    SUCCEED();
}

TEST_CASE("the log callback receives engine messages", "[abi]") {
    struct Capture {
        int count = 0;
        LogLevel highest = LogLevel::Trace;
    } capture;

    EngineDesc desc{};
    desc.logLevel = LogLevel::Trace;
    desc.logUserData = &capture;
    desc.logCallback = [](LogLevel level, const char* message, void* userData) {
        auto* c = static_cast<Capture*>(userData);
        ++c->count;
        if (level > c->highest) {
            c->highest = level;
        }
        CHECK(message != nullptr);
    };

    ICadEngine* engine = CadGeom_CreateEngine(desc);
    REQUIRE(engine != nullptr);
    engine->Release();

    // Creation and destruction both log at Info.
    CHECK(capture.count >= 2);
}
