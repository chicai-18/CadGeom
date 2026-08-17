// CadGeom — POD types shared across the ABI boundary.
//
// Everything in this header is a plain aggregate or a scoped enum with an
// explicit underlying type. No STL, no virtuals, no ownership, no allocation.
// Whatever lands here is frozen the moment it ships: hosts compile these
// layouts into their own code (docs/architecture.md §2.2 rule 1).
//
// Conventions:
//   * Coordinate system is Z-up right-handed (CAD convention). glTF's Y-up is
//     converted in the IO layer, never here.
//   * Kernel-facing geometry is double; anything a shader consumes is float.
//   * Matrices are column-major, m[column * 4 + row], matching GLSL.
//   * Angles are radians unless the field name says Deg.
#ifndef CADGEOM_TYPES_H
#define CADGEOM_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include <cadgeom/Version.h>

namespace cadgeom {

// ---------------------------------------------------------------------------
// Result codes — no exception ever crosses the boundary (§2.2 rule 4).
// ---------------------------------------------------------------------------

enum class CgResult : int32_t {
    Ok = 0,
    Unknown = -1,
    NotImplemented = -2,  ///< Reached a milestone stub. Expected during M0.
    InvalidArgument = -3,
    InvalidHandle = -4,
    NotFound = -5,
    NotSupported = -6,
    OutOfMemory = -7,
    InvalidState = -8,
    VersionMismatch = -9,
    IoError = -10,
    ParseError = -11,
    GeometryError = -12,
    RenderError = -13,
    DeviceLost = -14,
};

inline bool CgSucceeded(CgResult r) { return r == CgResult::Ok; }
inline bool CgFailed(CgResult r) { return r != CgResult::Ok; }

// ---------------------------------------------------------------------------
// Span — the only array-passing convention on the boundary (§2.2 rule 1).
// Non-owning pointer + count. Caller keeps the memory alive for the call.
// ---------------------------------------------------------------------------

template <typename T>
struct CgSpan {
    T* data;
    size_t count;
};

// ---------------------------------------------------------------------------
// Handles — opaque, stable, copyable. Zero is always "none".
// ---------------------------------------------------------------------------

struct EntityId {
    uint64_t value;
};
struct ShapeId {
    uint64_t value;
};

constexpr EntityId kInvalidEntity{0};
constexpr ShapeId kInvalidShape{0};

inline bool operator==(EntityId a, EntityId b) { return a.value == b.value; }
inline bool operator!=(EntityId a, EntityId b) { return a.value != b.value; }
inline bool operator==(ShapeId a, ShapeId b) { return a.value == b.value; }
inline bool operator!=(ShapeId a, ShapeId b) { return a.value != b.value; }
inline bool IsValid(EntityId e) { return e.value != 0; }
inline bool IsValid(ShapeId s) { return s.value != 0; }

// ---------------------------------------------------------------------------
// Math aggregates
// ---------------------------------------------------------------------------

struct Vec2d {
    double x, y;
};

struct Vec3d {
    double x, y, z;
};

struct Vec4d {
    double x, y, z, w;
};

/// Unit quaternion, xyzw order.
struct Quatd {
    double x, y, z, w;
};

/// Column-major 4x4. m[c * 4 + r].
struct Mat4d {
    double m[16];
};

/// Linear-space RGBA.
struct Color {
    float r, g, b, a;
};

struct Aabb {
    Vec3d min;
    Vec3d max;
};

/// Infinite plane through `origin` with unit `normal`.
struct Plane {
    Vec3d origin;
    Vec3d normal;
};

/// `dir` is expected normalized.
struct Ray {
    Vec3d origin;
    Vec3d dir;
};

/// TRS decomposition. The engine composes as world = parent * T * R * S.
/// Default-constructs to identity.
struct Transform {
    Vec3d translation{0.0, 0.0, 0.0};
    Quatd rotation{0.0, 0.0, 0.0, 1.0};
    Vec3d scale{1.0, 1.0, 1.0};
};

/// The plane 2D creation tools drop points onto (§6.1). uAxis/vAxis are unit
/// vectors spanning the plane; normal = uAxis x vAxis.
struct WorkPlane {
    Vec3d origin;
    Vec3d normal;
    Vec3d uAxis;
    Vec3d vAxis;
};

// ---------------------------------------------------------------------------
// Geometry description
// ---------------------------------------------------------------------------

enum class ShapeType : int32_t {
    None = 0,
    Point,
    Line,
    Circle,
    Arc,
    Rectangle,
    Polyline,
    Mesh,   ///< Imported triangle soup, no parametric definition.
    Solid,  ///< Result of extrude / boolean; carries topology.
};

/// Tessellation budget. Zero means "engine default".
struct TessParams {
    double chordTolerance{0.0};    ///< Max deviation of a chord from true geometry, model units.
    double angularTolerance{0.0};  ///< Max angle between adjacent segments, radians.
};

struct ExtrudeOptions {
    double draftAngle{0.0};       ///< Radians, 0 = straight walls.
    bool bothDirections{false};   ///< Extrude symmetrically about the profile plane.
    bool capEnds{true};           ///< Produce bottom/top faces (false yields a shell).
};

// ---------------------------------------------------------------------------
// Appearance
// ---------------------------------------------------------------------------

enum class LineStyle : int32_t {
    Solid = 0,
    Dashed,
    Dotted,
    DashDot,
    Center,  ///< Long-short-long, for axes/centerlines.
    Hidden,  ///< Short even dashes, for occluded edges.
};

enum class RenderMode : int32_t {
    Shaded = 0,
    ShadedWithEdges,  ///< Default for CAD: solid + black feature edges.
    Wireframe,
    HiddenLine,
};

/// Per-entity display overrides.
struct EntityStyle {
    Color color{0.75f, 0.76f, 0.78f, 1.0f};
    Color edgeColor{0.10f, 0.10f, 0.12f, 1.0f};
    float lineWidth{1.5f};  ///< Screen-space pixels.
    float pointSize{6.0f};  ///< Screen-space pixels.
    LineStyle lineStyle{LineStyle::Solid};
    bool visible{true};
    bool selectable{true};
    bool castsShadow{true};
};

// ---------------------------------------------------------------------------
// Viewport / surface
// ---------------------------------------------------------------------------

enum class SurfaceKind : int32_t {
    /// Engine owns a GLFW window. Useful for demos and debugging.
    Glfw = 0,
    /// Host owns an HWND; the engine never touches the message loop.
    NativeWin32,
    NativeXlib,
    NativeWayland,
    NativeCocoa,
    /// Off-screen only. No swapchain, no presentation.
    Headless,
};

/// `nativeWindow` / `nativeDisplay` carry the platform handles:
///   NativeWin32   -> nativeWindow = HWND,        nativeDisplay = HINSTANCE (optional)
///   NativeXlib    -> nativeWindow = ::Window,    nativeDisplay = Display*
///   NativeWayland -> nativeWindow = wl_surface*, nativeDisplay = wl_display*
///   NativeCocoa   -> nativeWindow = NSView*/CAMetalLayer*
///   Glfw/Headless -> both null; the engine creates what it needs.
struct SurfaceDesc {
    SurfaceKind kind{SurfaceKind::Glfw};
    void* nativeWindow{nullptr};
    void* nativeDisplay{nullptr};
    uint32_t width{1280};
    uint32_t height{720};
    const char* title{nullptr};  ///< Glfw only. UTF-8. May be null.
};

enum class ProjectionMode : int32_t {
    Orthographic = 0,  ///< CAD default.
    Perspective,
};

struct ViewportDesc {
    SurfaceDesc surface{};
    ProjectionMode projection{ProjectionMode::Orthographic};
    Color background{0.16f, 0.17f, 0.19f, 1.0f};
    bool showGrid{true};
    bool vsync{true};
    /// MSAA samples; 0 or 1 = off. Rounded *down* to what the device supports
    /// (ask for 8 on a 4x device and you get 4), so read the result back from
    /// ICadEngine2::GetSampleCount() if it matters.
    uint32_t sampleCount{1};
};

// ---------------------------------------------------------------------------
// Input — the host forwards these; the engine never reads the OS queue.
// ---------------------------------------------------------------------------

enum class MouseButton : int32_t {
    None = 0,
    Left,
    Right,
    Middle,
};

enum class MouseAction : int32_t {
    Move = 0,
    Down,
    Up,
    DoubleClick,
    Wheel,
    Enter,
    Leave,
};

/// Bit flags, OR-combined into the `mods` fields below.
enum KeyMod : uint32_t {
    KeyMod_None = 0u,
    KeyMod_Shift = 1u << 0,
    KeyMod_Ctrl = 1u << 1,
    KeyMod_Alt = 1u << 2,
    KeyMod_Super = 1u << 3,
};

/// Coordinates are in viewport pixels, origin at the top-left, y down —
/// matching every windowing system the host is likely to forward from.
struct MouseEvent {
    MouseButton button;
    MouseAction action;
    double x;
    double y;
    double wheelDelta;  ///< Notches; positive = away from user = zoom in.
    uint32_t mods;      ///< KeyMod bit flags.
};

enum class KeyAction : int32_t {
    Down = 0,
    Up,
    Repeat,
};

/// Printable keys use their uppercase ASCII code; the rest use Key_* below.
enum Key : int32_t {
    Key_Unknown = 0,
    Key_Escape = 256,
    Key_Enter,
    Key_Tab,
    Key_Backspace,
    Key_Insert,
    Key_Delete,
    Key_Right,
    Key_Left,
    Key_Down,
    Key_Up,
    Key_Home,
    Key_End,
    Key_F1 = 290,
};

struct KeyEvent {
    int32_t key;
    KeyAction action;
    uint32_t mods;
};

// ---------------------------------------------------------------------------
// Picking (§6.3)
// ---------------------------------------------------------------------------

enum class PickKind : int32_t {
    None = 0,
    Vertex,
    Edge,
    Face,
    Entity,  ///< Whole-object hit with no sub-element resolution.
};

/// Bit flags restricting what a pick may return, high priority first.
enum PickFilter : uint32_t {
    PickFilter_None = 0u,
    PickFilter_Vertex = 1u << 0,
    PickFilter_Edge = 1u << 1,
    PickFilter_Face = 1u << 2,
    PickFilter_All = 0x7u,
};

/// `subIndex` indexes into the topology list named by `kind`. A face hit
/// carries the index that makes "click a face -> set work plane -> draw on it"
/// possible, which is the CAD workflow the whole interaction layer serves.
struct PickResult {
    EntityId entity;
    PickKind kind;
    uint32_t subIndex;
    Vec3d point;
    Vec3d normal;
    double distance;  ///< Along the pick ray, from the camera.
};

enum SnapType : uint32_t {
    Snap_None = 0u,
    Snap_Endpoint = 1u << 0,
    Snap_Midpoint = 1u << 1,
    Snap_Center = 1u << 2,
    Snap_Quadrant = 1u << 3,
    Snap_Intersection = 1u << 4,
    Snap_Perpendicular = 1u << 5,
    Snap_Grid = 1u << 6,
    Snap_All = 0x7Fu,
};

struct SnapResult {
    SnapType type;
    Vec3d point;
    EntityId entity;
};

// ---------------------------------------------------------------------------
// 单位系统（M6）
//
// 引擎内部只认「模型单位」这一种长度 —— 几何、包围盒、容差、文件里的坐标，全是
// 它。单位系统管的是**显示**：把那个数换算成人读的数字和后缀。换个显示单位不会
// 动任何一个顶点。
// ---------------------------------------------------------------------------

/// @brief 长度单位。`modelUnit` 说的是「模型里的 1.0 是多长」，`displayUnit`
///        说的是「读出来用什么」，两者互不相干。
enum class LengthUnit : int32_t {
    Millimetre = 0,
    Centimetre,
    Metre,
    Kilometre,
    Inch,
    Foot,
    Yard,
    Mile,
};

enum class AngleUnit : int32_t {
    Degrees = 0,
    Radians,
};

/// @brief 一个引擎的显示单位设置。
/// @note `modelUnit` 是 CAD 里那件必须说清楚的事：同一个 `1.0`，在毫米图纸上
///       是一毫米，在米制场景里是一米。导入导出的缩放、测量读数和 HUD 上的网格
///       间距都从它出发。
struct UnitSettings {
    LengthUnit modelUnit{LengthUnit::Millimetre};
    LengthUnit displayUnit{LengthUnit::Millimetre};
    AngleUnit angleUnit{AngleUnit::Degrees};
    /// 小数位数，负数按 0 处理。
    int32_t linearPrecision{2};
    int32_t angularPrecision{1};
    /// 格式化结果带不带 " mm" 这样的后缀。
    bool showUnitSuffix{true};
};

// ---------------------------------------------------------------------------
// 扩展接口（§2.2 第 3 条）
//
// 已发布接口的 vtable 是冻结的，新能力因此走 ICadEngine::GetExtension()：给一个
// id，拿一个独立接口。宿主拿到 null 就是「这个版本的库没有这件东西」，而不是崩。
// ---------------------------------------------------------------------------

enum ExtensionId : uint32_t {
    ExtensionId_None = 0u,
    /// ICadEngine2 —— M6 的单位系统、吸附参考点与视口附加查询。
    ExtensionId_Engine2 = 1u,
};

// ---------------------------------------------------------------------------
// IO
// ---------------------------------------------------------------------------

enum class FileFormat : int32_t {
    Auto = 0,  ///< Dispatch on file extension.
    Obj,
    Gltf,  ///< .gltf, JSON + external buffers
    Glb,   ///< .glb, single binary container
};

struct ImportOptions {
    FileFormat format{FileFormat::Auto};
    double scaleToModelUnits{1.0};   ///< 0 or 1 = no scaling.
    bool mergeIntoScene{true};       ///< false clears the scene first.
    bool readParametricExtras{true}; ///< Restore CadGeom params from glTF `extras`.
};

struct ExportOptions {
    FileFormat format{FileFormat::Auto};
    bool writeParametricExtras{true};  ///< Round-trip fidelity for our own reader.
    bool selectionOnly{false};
    bool embedTextures{true};
};

// ---------------------------------------------------------------------------
// Engine creation
// ---------------------------------------------------------------------------

enum class KernelType : int32_t {
    Simple = 0,  ///< Built-in lightweight kernel.
    Occt,        ///< Reserved; not built in this release.
};

enum class LogLevel : int32_t {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Off,
};

/// Log sink. Called from whichever thread produced the message; the engine
/// serializes calls so an implementation need not lock. `message` is UTF-8 and
/// only valid for the duration of the call.
typedef void (*LogCallback)(LogLevel level, const char* message, void* userData);

/// The default member initializers are deliberate: they are compiled into the
/// *host*, so `EngineDesc{ .enableValidation = true }` stamps in the API
/// version the host's headers were built against, and CadGeom_CreateEngine
/// rejects the call if the loaded DLL disagrees.
struct EngineDesc {
    /// Must be CADGEOM_API_VERSION as seen by the host's headers.
    uint32_t apiVersion{CADGEOM_API_VERSION};
    const char* applicationName{nullptr};  ///< UTF-8, may be null.
    KernelType kernelType{KernelType::Simple};
    bool enableValidation{false};  ///< Vulkan validation layers + extra internal checks.
    LogLevel logLevel{LogLevel::Info};
    LogCallback logCallback{nullptr};  ///< null = messages are dropped.
    void* logUserData{nullptr};
};

} // namespace cadgeom

#endif // CADGEOM_TYPES_H
