#include "interact/WorkPlane.h"

#include <cmath>

namespace cadgeom::interact {

bool RayHitsWorkPlane(const WorkPlane& plane, const Ray& ray, Vec3d& out) {
    return RayPlaneIntersect(ray, plane.origin, plane.normal, out);
}

Vec2d ToPlaneUV(const WorkPlane& plane, const Vec3d& world) {
    const Vec3d offset = world - plane.origin;
    return Vec2d{Dot(offset, plane.uAxis), Dot(offset, plane.vAxis)};
}

Vec3d FromPlaneUV(const WorkPlane& plane, const Vec2d& uv) {
    return plane.origin + plane.uAxis * uv.x + plane.vAxis * uv.y;
}

Vec3d SnapToGrid(const WorkPlane& plane, const Vec3d& world, double spacing) {
    if (!(spacing > 0.0)) {
        return world;
    }
    // Rounded in plane coordinates, not world ones: on a tilted work plane the
    // lattice has to follow the plane, or the snapped point drifts off it.
    const Vec2d uv = ToPlaneUV(plane, world);
    const Vec2d snapped{std::round(uv.x / spacing) * spacing,
                        std::round(uv.y / spacing) * spacing};
    return FromPlaneUV(plane, snapped);
}

WorkPlane StandardWorkPlaneXY(const Vec3d& origin) {
    return WorkPlane{origin, Vec3d{0.0, 0.0, 1.0}, Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}};
}

WorkPlane StandardWorkPlaneYZ(const Vec3d& origin) {
    return WorkPlane{origin, Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}, Vec3d{0.0, 0.0, 1.0}};
}

WorkPlane StandardWorkPlaneZX(const Vec3d& origin) {
    return WorkPlane{origin, Vec3d{0.0, 1.0, 0.0}, Vec3d{0.0, 0.0, 1.0}, Vec3d{1.0, 0.0, 0.0}};
}

} // namespace cadgeom::interact
