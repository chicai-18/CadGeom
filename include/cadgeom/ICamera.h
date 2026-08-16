// CadGeom — viewport camera.
#ifndef CADGEOM_ICAMERA_H
#define CADGEOM_ICAMERA_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

/// Canonical CAD view directions, named for the face of the model you end up
/// looking at. Z-up right-handed.
enum class StandardView : int32_t {
    Top = 0,     ///< Looking down -Z
    Bottom,      ///< Looking up +Z
    Front,       ///< Looking along +Y
    Back,        ///< Looking along -Y
    Right,       ///< Looking along -X
    Left,        ///< Looking along +X
    Isometric,   ///< Front-top-right
};

/// Orbit camera owned by its viewport (docs/architecture.md §6.4).
///
/// The camera is defined by a target point, a distance and two angles rather
/// than a free-floating matrix: orbiting about the model is the dominant CAD
/// navigation and a turntable model makes it stable and predictable.
///
/// All positions are double. The renderer subtracts the camera origin before
/// converting to float so far-from-origin models do not jitter.
class CADGEOM_API ICamera {
public:
    virtual void SetProjection(ProjectionMode mode) = 0;
    virtual ProjectionMode GetProjection() const = 0;

    /// Vertical field of view in radians. Perspective only.
    virtual void SetFieldOfView(double radians) = 0;
    virtual double GetFieldOfView() const = 0;

    /// Height of the view volume in model units. Orthographic only.
    virtual void SetOrthoHeight(double height) = 0;
    virtual double GetOrthoHeight() const = 0;

    virtual void SetNearFar(double nearPlane, double farPlane) = 0;
    virtual void GetNearFar(double& nearPlane, double& farPlane) const = 0;

    // -- Pose ---------------------------------------------------------------

    virtual void GetPosition(Vec3d& out) const = 0;
    virtual void GetTarget(Vec3d& out) const = 0;
    virtual void GetUp(Vec3d& out) const = 0;
    virtual void GetForward(Vec3d& out) const = 0;

    virtual void LookAt(const Vec3d& eye, const Vec3d& target, const Vec3d& up) = 0;
    virtual void SetStandardView(StandardView view) = 0;

    // -- Navigation ---------------------------------------------------------

    /// Turntable orbit about the target, in radians.
    virtual void Orbit(double yaw, double pitch) = 0;
    /// Pan in the camera's screen plane, in pixels.
    virtual void Pan(double dxPixels, double dyPixels) = 0;
    /// Positive zooms in. When `towardScreenX/Y` is a valid viewport point the
    /// zoom keeps that point fixed under the cursor.
    virtual void Zoom(double amount, double towardScreenX, double towardScreenY) = 0;

    /// Frames a box (or the whole scene when `bounds` is null) with a margin
    /// factor; 1.0 fits exactly, 1.1 leaves 10% of air.
    virtual void ZoomToFit(const Aabb* bounds, double margin) = 0;

    // -- Matrices & projection helpers --------------------------------------

    /// View space is right-handed with the camera looking along -Z.
    virtual void GetViewMatrix(Mat4d& out) const = 0;

    /// Projects into Vulkan's clip space: x and y in [-1, 1] with **+y down**,
    /// and z in [0, 1] — not OpenGL's [-1, 1]. The y flip lives here rather
    /// than in a negative viewport height, so this is the matrix the engine
    /// actually drew with and ScreenToRay/WorldToScreen agree with it.
    virtual void GetProjectionMatrix(Mat4d& out) const = 0;

    /// Viewport pixel (top-left origin) to a world-space ray.
    virtual void ScreenToRay(double x, double y, Ray& out) const = 0;
    /// World point to viewport pixel. False when the point is behind the eye.
    virtual bool WorldToScreen(const Vec3d& world, Vec2d& out) const = 0;

    /// World units covered by one pixel at `atWorldPoint`. This is what turns
    /// a "6 pixel" pick tolerance into a world-space distance.
    virtual double GetPixelWorldSize(const Vec3d& atWorldPoint) const = 0;

protected:
    virtual ~ICamera() = default;
};

} // namespace cadgeom

#endif // CADGEOM_ICAMERA_H
