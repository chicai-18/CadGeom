#include "interact/Gizmo.h"

#include <cadgeom/ICamera.h>

#include "geom/Intersect.h"

#include <algorithm>

namespace cadgeom::interact {
namespace {

/// 轴的长度，屏幕像素。够大到好抓，又不至于在小视口里糊满屏幕。
constexpr double kAxisPixels = 88.0;
/// 抓取容差，像素。比拾取的 6 像素宽一点：Gizmo 是控件，抓不住比误抓更烦人。
constexpr double kGrabPixels = 9.0;
/// 平面方块离原点的距离和它的边长，都是轴长的比例。
constexpr double kPlaneOffset = 0.34;
constexpr double kPlaneSize = 0.24;
/// 旋转环的命中测试采样段数。
constexpr uint32_t kRingSegments = 64;
/// 平面手柄和旋转环与视线夹角太小就不可用：拖拽平面几乎平行于视线时，鼠标移动
/// 一个像素能让解出来的点跑到无穷远。
constexpr double kMinFacing = 0.18;

/// 颜色是线性光的（渲染器在线性空间合成），所以这些数看起来比屏幕上暗。
constexpr Color kAxisColors[3] = {{0.90f, 0.10f, 0.12f, 1.0f},
                                  {0.15f, 0.72f, 0.20f, 1.0f},
                                  {0.15f, 0.35f, 0.95f, 1.0f}};
constexpr Color kHighlightColor{1.00f, 0.80f, 0.10f, 1.0f};
constexpr Color kOriginColor{0.95f, 0.95f, 0.95f, 1.0f};

constexpr float kAxisWidth = 2.5f;
constexpr float kActiveWidth = 4.0f;

const Vec3d kWorldAxes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

int AxisIndex(GizmoHandle handle) {
    switch (handle) {
        case GizmoHandle::AxisX:
        case GizmoHandle::PlaneYZ: return 0;
        case GizmoHandle::AxisY:
        case GizmoHandle::PlaneZX: return 1;
        case GizmoHandle::AxisZ:
        case GizmoHandle::PlaneXY: return 2;
        default:                   return -1;
    }
}

double PointSegmentPixels(const Vec2d& a, const Vec2d& b, double x, double y) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lenSq = dx * dx + dy * dy;
    double t = 0.0;
    if (lenSq > 1e-12) {
        t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / lenSq, 0.0, 1.0);
    }
    const double px = a.x + dx * t - x;
    const double py = a.y + dy * t - y;
    return std::sqrt(px * px + py * py);
}

/// 凸四边形内外判定：四条边的叉积同号即在内部。
bool PointInQuad(const Vec2d quad[4], double x, double y) {
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const Vec2d& a = quad[i];
        const Vec2d& b = quad[(i + 1) & 3];
        const double cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (cross == 0.0) {
            continue;
        }
        const int s = cross > 0.0 ? 1 : -1;
        if (sign == 0) {
            sign = s;
        } else if (sign != s) {
            return false;
        }
    }
    return sign != 0;
}

/// 这个手柄在当前视角下够不够正对着看。
bool Facing(const ICamera& camera, const Vec3d& normal) {
    Vec3d forward{};
    camera.GetForward(forward);
    return std::fabs(Dot(Normalized(normal), forward)) >= kMinFacing;
}

} // namespace

Vec3d Gizmo::AxisOf(GizmoHandle handle) {
    const int index = AxisIndex(handle);
    return index >= 0 ? kWorldAxes[index] : Vec3d{0.0, 0.0, 1.0};
}

bool Gizmo::IsPlaneHandle(GizmoHandle handle) {
    return handle == GizmoHandle::PlaneYZ || handle == GizmoHandle::PlaneZX ||
           handle == GizmoHandle::PlaneXY;
}

double Gizmo::WorldSize(const ICamera& camera) const {
    return camera.GetPixelWorldSize(origin_) * kAxisPixels;
}

GizmoHandle Gizmo::HitTest(const ICamera& camera, double x, double y) const {
    const double size = WorldSize(camera);
    if (!(size > 0.0)) {
        return GizmoHandle::None;
    }

    if (mode_ == GizmoMode::Rotate) {
        GizmoHandle best = GizmoHandle::None;
        double bestPixels = kGrabPixels;
        for (int axis = 0; axis < 3; ++axis) {
            const auto handle = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + axis);
            if (!Facing(camera, kWorldAxes[axis])) {
                continue;  // 侧看的环退化成一条线，抓住了也拖不动。
            }
            const WorkPlane frame = MakeWorkPlane(origin_, kWorldAxes[axis]);
            Vec2d previous{};
            bool havePrevious = false;
            for (uint32_t i = 0; i <= kRingSegments; ++i) {
                const double angle = kTwoPi * static_cast<double>(i) / kRingSegments;
                const Vec3d world = frame.origin + frame.uAxis * (size * std::cos(angle)) +
                                    frame.vAxis * (size * std::sin(angle));
                Vec2d pixel{};
                if (!camera.WorldToScreen(world, pixel)) {
                    havePrevious = false;
                    continue;
                }
                if (havePrevious) {
                    const double d = PointSegmentPixels(previous, pixel, x, y);
                    if (d < bestPixels) {
                        bestPixels = d;
                        best = handle;
                    }
                }
                previous = pixel;
                havePrevious = true;
            }
        }
        return best;
    }

    // 平面方块先测：它们贴在原点边上，和三根轴的根部重叠。
    for (int axis = 0; axis < 3; ++axis) {
        const auto handle = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::PlaneYZ) + axis);
        if (!Facing(camera, kWorldAxes[axis])) {
            continue;
        }
        const Vec3d u = kWorldAxes[(axis + 1) % 3];
        const Vec3d v = kWorldAxes[(axis + 2) % 3];
        const double inner = size * kPlaneOffset;
        const double outer = inner + size * kPlaneSize;
        const Vec3d corners[4] = {origin_ + u * inner + v * inner, origin_ + u * outer + v * inner,
                                  origin_ + u * outer + v * outer, origin_ + u * inner + v * outer};
        Vec2d quad[4];
        bool visible = true;
        for (int i = 0; i < 4; ++i) {
            if (!camera.WorldToScreen(corners[i], quad[i])) {
                visible = false;
                break;
            }
        }
        if (visible && PointInQuad(quad, x, y)) {
            return handle;
        }
    }

    GizmoHandle best = GizmoHandle::None;
    double bestPixels = kGrabPixels;
    Vec2d originPixel{};
    if (!camera.WorldToScreen(origin_, originPixel)) {
        return GizmoHandle::None;
    }
    for (int axis = 0; axis < 3; ++axis) {
        Vec2d tip{};
        if (!camera.WorldToScreen(origin_ + kWorldAxes[axis] * size, tip)) {
            continue;
        }
        const double d = PointSegmentPixels(originPixel, tip, x, y);
        if (d < bestPixels) {
            bestPixels = d;
            best = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + axis);
        }
    }
    return best;
}

bool Gizmo::BeginDrag(const ICamera& camera, GizmoHandle handle, double x, double y) {
    if (handle == GizmoHandle::None) {
        return false;
    }

    Ray ray{};
    camera.ScreenToRay(x, y, ray);
    const Vec3d axis = AxisOf(handle);

    if (mode_ == GizmoMode::Rotate) {
        Vec3d hit{};
        if (!RayPlaneIntersect(ray, origin_, axis, hit)) {
            return false;
        }
        const WorkPlane frame = MakeWorkPlane(origin_, axis);
        refU_ = frame.uAxis;
        refV_ = frame.vAxis;
        const Vec3d radial = hit - origin_;
        if (LengthSq(radial) < kEpsilon) {
            return false;  // 抓在轴心上，没有角度可言。
        }
        lastAngle_ = std::atan2(Dot(radial, refV_), Dot(radial, refU_));
        totalAngle_ = 0.0;
    } else if (IsPlaneHandle(handle)) {
        if (!RayPlaneIntersect(ray, origin_, axis, startPoint_)) {
            return false;
        }
    } else {
        double onAxis = 0.0;
        double onRay = 0.0;
        if (!geom::ClosestLineLine(origin_, axis, ray.origin, ray.dir, onAxis, onRay)) {
            return false;  // 视线正对着这根轴，沿它拖没有解。
        }
        startParam_ = onAxis;
    }

    handle_ = handle;
    dragging_ = true;
    return true;
}

bool Gizmo::UpdateDrag(const ICamera& camera, double x, double y, Vec3d& translation,
                       Quatd& rotation) {
    translation = Vec3d{0.0, 0.0, 0.0};
    rotation = QuatIdentity();
    if (!dragging_) {
        return false;
    }

    Ray ray{};
    camera.ScreenToRay(x, y, ray);
    const Vec3d axis = AxisOf(handle_);

    if (mode_ == GizmoMode::Rotate) {
        Vec3d hit{};
        if (!RayPlaneIntersect(ray, origin_, axis, hit)) {
            rotation = QuatFromAxisAngle(axis, totalAngle_);
            return false;
        }
        const Vec3d radial = hit - origin_;
        if (LengthSq(radial) < kEpsilon) {
            rotation = QuatFromAxisAngle(axis, totalAngle_);
            return false;
        }
        const double angle = std::atan2(Dot(radial, refV_), Dot(radial, refU_));
        // 每帧只累加一个增量并把它折进 (-pi, pi]，转过一整圈才不会突然反向。
        double delta = angle - lastAngle_;
        while (delta > kPi) {
            delta -= kTwoPi;
        }
        while (delta < -kPi) {
            delta += kTwoPi;
        }
        totalAngle_ += delta;
        lastAngle_ = angle;
        rotation = QuatFromAxisAngle(axis, totalAngle_);
        return true;
    }

    if (IsPlaneHandle(handle_)) {
        Vec3d hit{};
        if (!RayPlaneIntersect(ray, origin_, axis, hit)) {
            return false;
        }
        translation = hit - startPoint_;
        return true;
    }

    double onAxis = 0.0;
    double onRay = 0.0;
    if (!geom::ClosestLineLine(origin_, axis, ray.origin, ray.dir, onAxis, onRay)) {
        return false;
    }
    translation = axis * (onAxis - startParam_);
    return true;
}

void Gizmo::BuildOverlay(IOverlayBuilder& overlay, const ICamera& camera,
                         GizmoHandle highlight) const {
    const double size = WorldSize(camera);
    if (!(size > 0.0)) {
        return;
    }

    const auto colorFor = [&](GizmoHandle handle, int axis) {
        return handle == highlight ? kHighlightColor : kAxisColors[axis];
    };
    const auto widthFor = [&](GizmoHandle handle) {
        return handle == highlight ? kActiveWidth : kAxisWidth;
    };

    if (mode_ == GizmoMode::Rotate) {
        for (int axis = 0; axis < 3; ++axis) {
            const auto handle =
                static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + axis);
            // 侧看的环画成虚线，明确告诉用户它现在抓不了。
            const bool usable = Facing(camera, kWorldAxes[axis]);
            overlay.AddCircle(Plane{origin_, kWorldAxes[axis]}, size, colorFor(handle, axis),
                              widthFor(handle), usable ? LineStyle::Solid : LineStyle::Dotted);
        }
        overlay.AddPoint(origin_, kOriginColor, 7.0f);
        return;
    }

    for (int axis = 0; axis < 3; ++axis) {
        const auto handle = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + axis);
        const Vec3d tip = origin_ + kWorldAxes[axis] * size;
        overlay.AddLine(origin_, tip, colorFor(handle, axis), widthFor(handle), LineStyle::Solid);
        // 箭头用一个点顶替：叠加层只有点和线，一个实心圆盘读起来和箭尖一样清楚。
        overlay.AddPoint(tip, colorFor(handle, axis), handle == highlight ? 11.0f : 8.0f);
    }

    for (int axis = 0; axis < 3; ++axis) {
        const auto planeHandle =
            static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::PlaneYZ) + axis);
        if (!Facing(camera, kWorldAxes[axis])) {
            continue;
        }
        const Vec3d u = kWorldAxes[(axis + 1) % 3];
        const Vec3d v = kWorldAxes[(axis + 2) % 3];
        const double inner = size * kPlaneOffset;
        const double outer = inner + size * kPlaneSize;
        const Vec3d corners[4] = {origin_ + u * inner + v * inner, origin_ + u * outer + v * inner,
                                  origin_ + u * outer + v * outer, origin_ + u * inner + v * outer};
        overlay.AddPolyline(CgSpan<const Vec3d>{corners, 4}, /*closed=*/true,
                            colorFor(planeHandle, axis), widthFor(planeHandle), LineStyle::Solid);
    }

    overlay.AddPoint(origin_, kOriginColor, 7.0f);
}

} // namespace cadgeom::interact
