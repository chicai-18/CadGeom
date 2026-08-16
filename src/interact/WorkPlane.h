// The plane 2D creation drops points onto (docs/architecture.md §6.1).
//
// A mouse position is a ray, and a ray has no single answer in 3D space. The
// work plane is what turns it into one. Every 2D creation tool works on the
// current plane, and leaving that abstraction out is what turns interactive
// creation into a pile of special cases.
//
// core/Math.h already builds and orthonormalises a WorkPlane; what lives here is
// the interaction side: hit a plane with a ray, move between plane coordinates
// and world coordinates, and snap to the lattice the grid is drawing.
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

namespace cadgeom::interact {

/// Where `ray` meets the plane. False when the ray runs parallel to it or the
/// hit is behind the ray's origin — both mean "the user is not pointing at the
/// drawing plane", which a tool has to be able to tell.
bool RayHitsWorkPlane(const WorkPlane& plane, const Ray& ray, Vec3d& out);

/// World point -> (u, v) in the plane's own frame. Any component of the point
/// off the plane is dropped.
Vec2d ToPlaneUV(const WorkPlane& plane, const Vec3d& world);

/// (u, v) -> world point on the plane.
Vec3d FromPlaneUV(const WorkPlane& plane, const Vec2d& uv);

/// Rounds a world point to the nearest lattice intersection of `spacing`,
/// measured in the plane's own frame so the lattice lines up with what the grid
/// shader draws. Returns the point unchanged when `spacing` is not positive.
Vec3d SnapToGrid(const WorkPlane& plane, const Vec3d& world, double spacing);

/// The three canonical construction planes, Z-up.
WorkPlane StandardWorkPlaneXY(const Vec3d& origin = Vec3d{0.0, 0.0, 0.0});
WorkPlane StandardWorkPlaneYZ(const Vec3d& origin = Vec3d{0.0, 0.0, 0.0});
WorkPlane StandardWorkPlaneZX(const Vec3d& origin = Vec3d{0.0, 0.0, 0.0});

} // namespace cadgeom::interact
