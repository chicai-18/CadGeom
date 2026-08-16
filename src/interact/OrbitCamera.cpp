#include "interact/OrbitCamera.h"

#include <algorithm>

namespace cadgeom::interact {
namespace {

/// Stops just short of straight down. Exactly vertical would make the world's
/// up axis useless as a reference for the view basis; a ten-thousandth of a
/// radian off is invisible and keeps one code path for every orientation.
constexpr double kMaxPitch = kHalfPi - 1e-4;

constexpr double kMinDistance = 1e-6;
constexpr double kMaxDistance = 1e12;

/// Fallback when asked to frame a scene that has nothing in it.
Aabb DefaultFitBounds() {
    return Aabb{Vec3d{-10.0, -10.0, 0.0}, Vec3d{10.0, 10.0, 10.0}};
}

} // namespace

OrbitCamera::OrbitCamera() = default;
OrbitCamera::~OrbitCamera() = default;

// ---------------------------------------------------------------------------
// Basis
// ---------------------------------------------------------------------------

Vec3d OrbitCamera::EyeDirection() const {
    const double cp = std::cos(pitch_);
    return Vec3d{cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_)};
}

Vec3d OrbitCamera::Right() const {
    return Normalized(Cross(Forward(), Vec3d{0.0, 0.0, 1.0}));
}

Vec3d OrbitCamera::Up() const {
    return Cross(Right(), Forward());
}

double OrbitCamera::Aspect() const {
    return viewportHeight_ > 0
               ? static_cast<double>(viewportWidth_) / static_cast<double>(viewportHeight_)
               : 1.0;
}

void OrbitCamera::ProjectionPlanes(double& nearPlane, double& farPlane) const {
    if (projection_ == ProjectionMode::Perspective) {
        nearPlane = near_;
        farPlane = far_;
        return;
    }
    // Orthographic has no perspective divide, so the depth range can straddle
    // the target: geometry behind the target has to stay visible, which a near
    // plane sitting at the eye would clip away.
    nearPlane = distance_ - far_;
    farPlane = distance_ + far_;
}

// ---------------------------------------------------------------------------
// Projection settings
// ---------------------------------------------------------------------------

void OrbitCamera::SetProjection(ProjectionMode mode) {
    if (projection_ == mode) {
        return;
    }
    // Match the framing across the switch, so toggling does not jump the zoom:
    // the orthographic height that covers the same ground as the perspective
    // frustum does at the target distance.
    if (mode == ProjectionMode::Orthographic) {
        orthoHeight_ = 2.0 * distance_ * std::tan(0.5 * fovY_);
    } else {
        distance_ = 0.5 * orthoHeight_ / std::tan(0.5 * fovY_);
        distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
    }
    projection_ = mode;
}

ProjectionMode OrbitCamera::GetProjection() const {
    return projection_;
}

void OrbitCamera::SetFieldOfView(double radians) {
    fovY_ = std::clamp(radians, 1.0 * kDegToRad, 179.0 * kDegToRad);
}

double OrbitCamera::GetFieldOfView() const {
    return fovY_;
}

void OrbitCamera::SetOrthoHeight(double height) {
    if (height > 0.0) {
        orthoHeight_ = height;
    }
}

double OrbitCamera::GetOrthoHeight() const {
    return orthoHeight_;
}

void OrbitCamera::SetNearFar(double nearPlane, double farPlane) {
    if (nearPlane > 0.0 && farPlane > nearPlane) {
        near_ = nearPlane;
        far_ = farPlane;
    }
}

void OrbitCamera::GetNearFar(double& nearPlane, double& farPlane) const {
    nearPlane = near_;
    farPlane = far_;
}

// ---------------------------------------------------------------------------
// Pose
// ---------------------------------------------------------------------------

void OrbitCamera::GetPosition(Vec3d& out) const {
    out = Position();
}

void OrbitCamera::GetTarget(Vec3d& out) const {
    out = target_;
}

void OrbitCamera::GetUp(Vec3d& out) const {
    out = Up();
}

void OrbitCamera::GetForward(Vec3d& out) const {
    out = Forward();
}

void OrbitCamera::LookAt(const Vec3d& eye, const Vec3d& target, const Vec3d& /*up*/) {
    // `up` is ignored by design: a turntable camera has no roll, so the only
    // thing an arbitrary up vector could do is introduce one and then lose it
    // on the next orbit.
    const Vec3d toEye = eye - target;
    const double length = Length(toEye);
    if (length < kMinDistance) {
        return;
    }

    target_ = target;
    distance_ = std::clamp(length, kMinDistance, kMaxDistance);

    const Vec3d dir = toEye / length;
    pitch_ = std::clamp(std::asin(std::clamp(dir.z, -1.0, 1.0)), -kMaxPitch, kMaxPitch);
    const double horizontal = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (horizontal > kEpsilon) {
        yaw_ = std::atan2(dir.y, dir.x);
    }
}

void OrbitCamera::SetStandardView(StandardView view) {
    // Named for the face of the model you end up looking at, Z-up. The yaw for
    // the two vertical views is what puts +Y up the screen, matching every CAD
    // package's top view.
    switch (view) {
        case StandardView::Top:    yaw_ = -kHalfPi;      pitch_ = kMaxPitch;  break;
        case StandardView::Bottom: yaw_ = -kHalfPi;      pitch_ = -kMaxPitch; break;
        case StandardView::Front:  yaw_ = -kHalfPi;      pitch_ = 0.0;        break;
        case StandardView::Back:   yaw_ = kHalfPi;       pitch_ = 0.0;        break;
        case StandardView::Right:  yaw_ = 0.0;           pitch_ = 0.0;        break;
        case StandardView::Left:   yaw_ = kPi;           pitch_ = 0.0;        break;
        case StandardView::Isometric:
            yaw_ = -45.0 * kDegToRad;
            pitch_ = 35.264 * kDegToRad;  // atan(1/sqrt(2)): the true isometric.
            break;
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void OrbitCamera::Orbit(double yaw, double pitch) {
    yaw_ += yaw;
    pitch_ = std::clamp(pitch_ + pitch, -kMaxPitch, kMaxPitch);

    // Keep yaw in a sane range so it cannot drift into the region where a
    // double stops resolving small increments.
    if (yaw_ > kTwoPi || yaw_ < -kTwoPi) {
        yaw_ = std::fmod(yaw_, kTwoPi);
    }
}

void OrbitCamera::Pan(double dxPixels, double dyPixels) {
    const double worldPerPixel = GetPixelWorldSize(target_);
    // Screen y points down; dragging down should pull the model down with the
    // cursor, which means moving the target up.
    target_ += (Right() * -dxPixels + Up() * dyPixels) * worldPerPixel;
}

bool OrbitCamera::PointOnFocalPlane(double screenX, double screenY, Vec3d& out) const {
    Ray ray{};
    ScreenToRay(screenX, screenY, ray);
    return RayPlaneIntersect(ray, target_, -Forward(), out);
}

void OrbitCamera::Zoom(double amount, double towardScreenX, double towardScreenY) {
    if (amount == 0.0) {
        return;
    }

    const bool anchored = towardScreenX >= 0.0 && towardScreenY >= 0.0 &&
                          towardScreenX < static_cast<double>(viewportWidth_) &&
                          towardScreenY < static_cast<double>(viewportHeight_);
    Vec3d before{};
    const bool haveAnchor = anchored && PointOnFocalPlane(towardScreenX, towardScreenY, before);

    // Geometric, not linear: every notch changes the view by the same ratio, so
    // zooming feels identical whether the model is a millimetre or a kilometre
    // across.
    const double factor = std::pow(0.8, amount);
    distance_ = std::clamp(distance_ * factor, kMinDistance, kMaxDistance);
    orthoHeight_ = std::clamp(orthoHeight_ * factor, kMinDistance, kMaxDistance);

    if (haveAnchor) {
        // Slide the target so the world point under the cursor stays under it.
        Vec3d after{};
        if (PointOnFocalPlane(towardScreenX, towardScreenY, after)) {
            target_ += before - after;
        }
    }
}

void OrbitCamera::ZoomToFit(const Aabb* bounds, double margin) {
    Aabb box{};
    if (bounds && !IsEmpty(*bounds)) {
        box = *bounds;
    } else if (!scene_ || !scene_->GetBounds(box) || IsEmpty(box)) {
        box = DefaultFitBounds();
    }

    const double safeMargin = margin > 0.0 ? margin : 1.0;
    // The bounding sphere, so the fit holds from every orbit angle instead of
    // only the one it was computed at.
    const double radius = std::max(0.5 * DiagonalLength(box), kMinDistance);

    target_ = Center(box);
    orthoHeight_ = std::clamp(2.0 * radius * safeMargin, kMinDistance, kMaxDistance);
    distance_ = std::clamp(radius * safeMargin / std::tan(0.5 * fovY_), kMinDistance, kMaxDistance);
}

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------

void OrbitCamera::GetViewMatrix(Mat4d& out) const {
    out = Mat4LookAt(Position(), target_, Vec3d{0.0, 0.0, 1.0});
}

void OrbitCamera::GetProjectionMatrix(Mat4d& out) const {
    double nearPlane = 0.0;
    double farPlane = 0.0;
    ProjectionPlanes(nearPlane, farPlane);

    if (projection_ == ProjectionMode::Perspective) {
        out = Mat4Perspective(fovY_, Aspect(), nearPlane, farPlane);
    } else {
        const double halfHeight = 0.5 * orthoHeight_;
        const double halfWidth = halfHeight * Aspect();
        out = Mat4Ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }
}

Mat4d OrbitCamera::ViewProjectionRelative() const {
    Mat4d projection{};
    GetProjectionMatrix(projection);
    // The eye at the origin, looking at the target expressed relative to it.
    const Mat4d view =
        Mat4LookAt(Vec3d{0.0, 0.0, 0.0}, target_ - Position(), Vec3d{0.0, 0.0, 1.0});
    return projection * view;
}

// ---------------------------------------------------------------------------
// Screen space
// ---------------------------------------------------------------------------

void OrbitCamera::ScreenToRay(double x, double y, Ray& out) const {
    Mat4d view{};
    Mat4d projection{};
    GetViewMatrix(view);
    GetProjectionMatrix(projection);
    const Mat4d inverse = Inverse(projection * view);

    // Clip space is y-down, and so are viewport pixels, so this needs no flip.
    const double ndcX = 2.0 * x / static_cast<double>(std::max(viewportWidth_, 1u)) - 1.0;
    const double ndcY = 2.0 * y / static_cast<double>(std::max(viewportHeight_, 1u)) - 1.0;

    const Vec3d nearPoint = TransformPoint(inverse, Vec3d{ndcX, ndcY, 0.0});
    const Vec3d farPoint = TransformPoint(inverse, Vec3d{ndcX, ndcY, 1.0});

    out.origin = nearPoint;
    out.dir = Normalized(farPoint - nearPoint);
}

bool OrbitCamera::WorldToScreen(const Vec3d& world, Vec2d& out) const {
    Mat4d view{};
    Mat4d projection{};
    GetViewMatrix(view);
    GetProjectionMatrix(projection);

    const Vec4d clip =
        TransformVec4(projection * view, Vec4d{world.x, world.y, world.z, 1.0});
    if (clip.w <= kEpsilon) {
        return false;  // Behind the eye, or on the plane through it.
    }

    out.x = (clip.x / clip.w * 0.5 + 0.5) * static_cast<double>(viewportWidth_);
    out.y = (clip.y / clip.w * 0.5 + 0.5) * static_cast<double>(viewportHeight_);
    return true;
}

double OrbitCamera::GetPixelWorldSize(const Vec3d& atWorldPoint) const {
    const double height = static_cast<double>(std::max(viewportHeight_, 1u));
    if (projection_ == ProjectionMode::Orthographic) {
        return orthoHeight_ / height;  // Constant everywhere, by definition.
    }
    // Depth along the view axis, not straight-line distance: that is what the
    // frustum's width is proportional to.
    const double depth = std::fabs(Dot(atWorldPoint - Position(), Forward()));
    return 2.0 * std::tan(0.5 * fovY_) * std::max(depth, kMinDistance) / height;
}

void OrbitCamera::SetViewportSize(uint32_t width, uint32_t height) {
    viewportWidth_ = std::max(width, 1u);
    viewportHeight_ = std::max(height, 1u);
}

} // namespace cadgeom::interact
