// Turntable camera (docs/architecture.md §6.4).
//
// State is a target, a distance and two angles rather than a free matrix.
// Orbiting about the model is the dominant CAD navigation, and a turntable
// cannot roll: the horizon stays level no matter how long the user drags, which
// a free-flight camera famously does not.
//
// Nothing here touches Vulkan. The camera hands the renderer a view-projection
// built with the eye at the origin, which is where camera-relative rendering
// starts (docs/architecture.md §4.3).
#pragma once

#include <cadgeom/ICamera.h>
#include <cadgeom/IScene.h>

#include "core/Math.h"
#include "core/ObjectTracker.h"

namespace cadgeom::interact {

class OrbitCamera final : public ICamera {
public:
    OrbitCamera();
    ~OrbitCamera() override;

    // -- ICamera ------------------------------------------------------------

    void SetProjection(ProjectionMode mode) override;
    ProjectionMode GetProjection() const override;

    void SetFieldOfView(double radians) override;
    double GetFieldOfView() const override;

    void SetOrthoHeight(double height) override;
    double GetOrthoHeight() const override;

    void SetNearFar(double nearPlane, double farPlane) override;
    void GetNearFar(double& nearPlane, double& farPlane) const override;

    void GetPosition(Vec3d& out) const override;
    void GetTarget(Vec3d& out) const override;
    void GetUp(Vec3d& out) const override;
    void GetForward(Vec3d& out) const override;

    void LookAt(const Vec3d& eye, const Vec3d& target, const Vec3d& up) override;
    void SetStandardView(StandardView view) override;

    void Orbit(double yaw, double pitch) override;
    void Pan(double dxPixels, double dyPixels) override;
    void Zoom(double amount, double towardScreenX, double towardScreenY) override;
    void ZoomToFit(const Aabb* bounds, double margin) override;

    void GetViewMatrix(Mat4d& out) const override;
    void GetProjectionMatrix(Mat4d& out) const override;

    void ScreenToRay(double x, double y, Ray& out) const override;
    bool WorldToScreen(const Vec3d& world, Vec2d& out) const override;

    double GetPixelWorldSize(const Vec3d& atWorldPoint) const override;

    // -- Engine-side --------------------------------------------------------

    void SetViewportSize(uint32_t width, uint32_t height);
    /// Lets ZoomToFit(nullptr, ...) mean "the whole scene".
    void SetScene(const IScene* scene) { scene_ = scene; }

    Vec3d Position() const { return target_ + EyeDirection() * distance_; }
    Vec3d Forward() const { return -EyeDirection(); }
    Vec3d Right() const;
    Vec3d Up() const;

    /// View-projection with the eye at the origin. Feeding the GPU this, and
    /// model matrices with the camera origin subtracted, is what keeps a model
    /// far from the origin from shimmering.
    Mat4d ViewProjectionRelative() const;

    /// World units per pixel at the target — the scale the grid and every
    /// pixel-sized tolerance are chosen from.
    double PixelWorldSizeAtTarget() const { return GetPixelWorldSize(target_); }

    double Distance() const { return distance_; }

private:
    /// Unit vector from the target towards the eye.
    Vec3d EyeDirection() const;
    /// Near and far as the projection matrix wants them, which for an
    /// orthographic camera has to straddle the target rather than start at it.
    void ProjectionPlanes(double& nearPlane, double& farPlane) const;
    bool PointOnFocalPlane(double screenX, double screenY, Vec3d& out) const;
    double Aspect() const;

    core::ObjectTracker tracker_;

    ProjectionMode projection_{ProjectionMode::Orthographic};
    double fovY_{45.0 * kDegToRad};
    double orthoHeight_{30.0};
    double near_{0.1};
    double far_{10000.0};

    Vec3d target_{0.0, 0.0, 0.0};
    double distance_{40.0};
    double yaw_{-45.0 * kDegToRad};
    double pitch_{35.264 * kDegToRad};  // Isometric.

    uint32_t viewportWidth_{1};
    uint32_t viewportHeight_{1};

    const IScene* scene_{nullptr};
};

} // namespace cadgeom::interact
