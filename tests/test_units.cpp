// M6 从扩展槽进来的那一批：单位系统、吸附参考点、以及两个补齐的内置工具。
//
// 全程不需要视口 —— 这些东西一件都不碰 GPU，所以它们在这套不依赖 Vulkan 的测试里
// 是可以完整验证的（docs/architecture.md §8）。

#include "TestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

using Catch::Approx;
using namespace cadgeom;

namespace {

/// @brief 取扩展接口，取不到就让这一整个 TEST_CASE 立刻失败。
ICadEngine2& Ext(ICadEngine& engine) {
    auto* ext = static_cast<ICadEngine2*>(engine.GetExtension(ExtensionId_Engine2));
    REQUIRE(ext != nullptr);
    return *ext;
}

std::string Formatted(const ICadEngine2& ext, double modelUnits) {
    char buffer[64];
    const uint32_t written = ext.FormatLength(modelUnits, buffer, sizeof(buffer));
    CHECK(written == std::strlen(buffer));
    return buffer;
}

} // namespace

TEST_CASE("the extension slot answers only for ids it knows", "[units][abi]") {
    cgtest::EngineFixture fixture;

    CHECK(fixture.Engine().GetExtension(ExtensionId_Engine2) != nullptr);
    // 不认识的 id 返回 null 而不是崩：宿主据此判断这个版本的库有没有那件东西
    // （§2.2 第 3 条）。
    CHECK(fixture.Engine().GetExtension(ExtensionId_None) == nullptr);
    CHECK(fixture.Engine().GetExtension(0xDEADBEEFu) == nullptr);
}

TEST_CASE("the default unit setting is a millimetre drawing", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    UnitSettings units{};
    ext.GetUnitSettings(units);
    CHECK(units.modelUnit == LengthUnit::Millimetre);
    CHECK(units.displayUnit == LengthUnit::Millimetre);
    CHECK(units.angleUnit == AngleUnit::Degrees);

    // 模型单位和显示单位相同时换算是恒等的 —— 这是最常见的情形，也是最容易被
    // 一个多余的乘法悄悄破坏的情形。
    CHECK(ext.ToDisplayLength(12.5) == Approx(12.5));
    CHECK(ext.ToModelLength(12.5) == Approx(12.5));
    CHECK(Formatted(ext, 12.5) == "12.50 mm");
}

TEST_CASE("changing the display unit changes the reading, not the geometry", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    // 一个 25 mm 的圆。之后它的半径一个数都不会变 —— 换的只是读出来的样子。
    IGeometryBuilder& builder = *fixture.Scene().GetGeometryBuilder();
    const EntityId circle =
        builder.MakeCircle(Plane{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}}, 25.0);
    REQUIRE(IsValid(circle));
    const uint64_t revisionBefore = fixture.Scene().GetRevision();

    UnitSettings units{};
    ext.GetUnitSettings(units);
    units.displayUnit = LengthUnit::Metre;
    units.linearPrecision = 3;
    ext.SetUnitSettings(units);

    CHECK(ext.ToDisplayLength(1000.0) == Approx(1.0));
    CHECK(Formatted(ext, 25.0) == "0.025 m");

    // 参数没动，场景的 revision 也没动：换显示单位不该让整个场景重传一遍显存。
    ShapeParams params{};
    REQUIRE(builder.GetParams(circle, params));
    CHECK(params.circle.radius == Approx(25.0));
    CHECK(fixture.Scene().GetRevision() == revisionBefore);
}

TEST_CASE("imperial units round-trip through the international inch", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    UnitSettings units{};
    ext.GetUnitSettings(units);
    units.modelUnit = LengthUnit::Millimetre;
    units.displayUnit = LengthUnit::Inch;
    units.linearPrecision = 4;
    ext.SetUnitSettings(units);

    // 1 in = 25.4 mm，整数关系，所以这里该分毫不差。
    CHECK(ext.ToDisplayLength(25.4) == Approx(1.0));
    CHECK(ext.ToModelLength(1.0) == Approx(25.4));
    CHECK(Formatted(ext, 25.4) == "1.0000 in");

    // 来回一趟不该丢精度。
    CHECK(ext.ToModelLength(ext.ToDisplayLength(123.456)) == Approx(123.456));
}

TEST_CASE("a model in metres reads out in millimetres", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    // modelUnit 是 CAD 里必须说清楚的那件事：同一个 1.0，在毫米图纸上是一毫米，
    // 在米制场景里是一米。
    UnitSettings units{};
    units.modelUnit = LengthUnit::Metre;
    units.displayUnit = LengthUnit::Millimetre;
    units.linearPrecision = 1;
    ext.SetUnitSettings(units);

    CHECK(ext.ToDisplayLength(1.0) == Approx(1000.0));
    CHECK(Formatted(ext, 0.0125) == "12.5 mm");
}

TEST_CASE("formatting is total: no suffix, no room, no crash", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    UnitSettings units{};
    units.showUnitSuffix = false;
    units.linearPrecision = 1;
    ext.SetUnitSettings(units);
    CHECK(Formatted(ext, 3.14159) == "3.1");

    SECTION("a buffer too small still comes back terminated") {
        char tiny[4];
        const uint32_t written = ext.FormatLength(123456.0, tiny, sizeof(tiny));
        CHECK(written == 3);
        CHECK(tiny[3] == '\0');
    }

    SECTION("no buffer at all is not a crash") {
        CHECK(ext.FormatLength(1.0, nullptr, 0) == 0);
        CHECK(ext.FormatAngle(1.0, nullptr, 16) == 0);
    }

    SECTION("negative precision is clamped rather than passed to printf") {
        UnitSettings odd{};
        odd.linearPrecision = -5;
        odd.showUnitSuffix = false;
        ext.SetUnitSettings(odd);
        CHECK(Formatted(ext, 7.6) == "8");
    }
}

TEST_CASE("angles honour their own unit", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    // 公共头里没有数学常量（那是 core/Math.h 的东西，不跨边界），所以这里直接写。
    constexpr double kQuarterTurn = 1.57079632679489661923;

    char buffer[64];
    UnitSettings units{};
    units.angularPrecision = 1;
    ext.SetUnitSettings(units);
    ext.FormatAngle(kQuarterTurn, buffer, sizeof(buffer));
    CHECK(std::string(buffer) == "90.0 deg");

    units.angleUnit = AngleUnit::Radians;
    units.angularPrecision = 3;
    ext.SetUnitSettings(units);
    ext.FormatAngle(kQuarterTurn, buffer, sizeof(buffer));
    CHECK(std::string(buffer) == "1.571 rad");
}

TEST_CASE("the snap reference point is engine state, not a lost parameter", "[units][snap]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    // 这是 M6 绕开冻结签名的那一手：IToolContext::SnapAt 传不进参考点，所以参考点
    // 变成了引擎的一份状态（docs/architecture.md §6.3）。
    Vec3d reference{};
    CHECK_FALSE(ext.GetSnapReference(reference));

    ext.SetSnapReference(Vec3d{1.0, 2.0, 3.0});
    REQUIRE(ext.GetSnapReference(reference));
    CHECK(reference.x == Approx(1.0));
    CHECK(reference.y == Approx(2.0));
    CHECK(reference.z == Approx(3.0));

    ext.ClearSnapReference();
    CHECK_FALSE(ext.GetSnapReference(reference));
}

TEST_CASE("every built-in tool id now resolves to a tool", "[units][tools]") {
    cgtest::EngineFixture fixture;
    IToolManager& tools = *fixture.Engine().GetToolManager();

    // M6 之前 Scale 和 Measure 都会报 NotImplemented。现在引擎认得的 id 里再没有
    // 给不出的那一类。
    const ToolId ids[] = {ToolId::Select,  ToolId::Point, ToolId::Line,   ToolId::Circle,
                          ToolId::Rectangle, ToolId::Polyline, ToolId::Extrude,
                          ToolId::Move,    ToolId::Rotate, ToolId::Scale, ToolId::Measure};
    for (const ToolId id : ids) {
        CHECK(CgSucceeded(tools.Activate(id)));
        CHECK(tools.GetActiveTool() == id);
        CHECK(fixture.Engine().GetToolManager()->GetActiveToolInterface() != nullptr);
    }

    // 宿主自己的 id 段依旧是「你没注册过」。
    CHECK(tools.Activate(static_cast<ToolId>(static_cast<int32_t>(ToolId::Custom) + 7)) ==
          CgResult::NotFound);
    fixture.Engine().ClearLastError();
}

TEST_CASE("Snap_Perpendicular is in the default snap mask", "[units][snap]") {
    cgtest::EngineFixture fixture;
    IToolManager& tools = *fixture.Engine().GetToolManager();

    // M3 把这一位留空是因为没地方传参考点；M6 补上了那条路，所以它默认就该是开的。
    CHECK((tools.GetSnapMask() & Snap_Perpendicular) != 0u);

    tools.SetSnapMask(Snap_Endpoint);
    CHECK(tools.GetSnapMask() == Snap_Endpoint);
    tools.SetSnapMask(Snap_All);
    CHECK((tools.GetSnapMask() & Snap_Perpendicular) != 0u);
}

TEST_CASE("measurement and HUD queries are honest without a viewport", "[units]") {
    cgtest::EngineFixture fixture;
    ICadEngine2& ext = Ext(fixture.Engine());

    Vec3d from{};
    Vec3d to{};
    double distance = -1.0;
    CHECK_FALSE(ext.GetMeasurement(from, to, distance));

    // 没有视口就没有 HUD、没有采样数，也没有状态栏 —— 但这些查询都得给一个答案，
    // 而不是崩掉。状态文本永远不为 null，这是头文件的承诺。
    CHECK(ext.IsHudVisible(nullptr) == false);
    CHECK(ext.GetSampleCount(nullptr) == 0u);
    REQUIRE(ext.GetStatusText(nullptr) != nullptr);
    ext.SetHudVisible(nullptr, false);  // 一个视口都没有，什么都不该发生。
}
