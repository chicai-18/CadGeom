// Parametric curve definitions (docs/architecture.md §3).
//
// These are the *definitions*, not the triangles: a circle is a plane, a radius
// and nothing else, and it stays that way for the lifetime of the shape. Turning
// one into segments is Tessellate()'s job, and the segment count comes from a
// tolerance rather than from a fixed number, so a 1 mm fillet and a 10 m arc
// both come out smooth without anyone tuning a constant.
//
// Everything here is pure math on double — no allocation beyond the output
// vector, no scene, no GPU. That is what keeps the kernel unit-testable without
// standing up a Vulkan device.
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

#include <vector>

namespace cadgeom::geom {

/// The in-plane reference frame a Plane implies. Angles on a circle or an arc
/// are measured from `uAxis`, counter-clockwise about the normal, so a shape's
/// start angle means the same thing every time it is re-tessellated.
///
/// Derived deterministically from the normal alone (see Perpendicular()), which
/// is what makes a Plane a complete definition rather than half of one.
WorkPlane FrameOf(const Plane& plane);

/// Number of chords a sweep of `sweepAngle` radians on a circle of `radius`
/// needs to stay within both tolerances. Always at least 2 for a full circle's
/// worth of sweep, and capped so a pathological tolerance cannot ask for a
/// million segments.
uint32_t ArcSegmentCount(double radius, double sweepAngle, const TessParams& tess);

/// Points along an arc, `segmentCount + 1` of them, from `startAngle` through
/// `sweepAngle`. A full circle passes `sweepAngle = 2pi` and drops the duplicate
/// closing point by asking for `closed`.
void SampleArc(const Plane& plane, double radius, double startAngle, double sweepAngle,
               uint32_t segmentCount, bool dropLastPoint, std::vector<Vec3d>& out);

/// The four corners of a rectangle, counter-clockwise about the plane normal
/// starting at (-w/2, -h/2). `uAxis` is projected into the plane first, so a
/// caller may hand over any direction that is not parallel to the normal.
void RectangleCorners(const Plane& plane, const Vec3d& uAxis, double width, double height,
                      Vec3d out[4]);

// ---------------------------------------------------------------------------
// Validation — every creation path funnels through these so the kernel and the
// public builder cannot disagree about what a legal shape is.
// ---------------------------------------------------------------------------

/// True when `normal` has enough length to define a plane.
bool IsUsablePlane(const Plane& plane);

/// Smallest length the kernel treats as non-degenerate, relative to `scale`.
/// Absolute epsilons are wrong for CAD: a model measured in metres and one
/// measured in microns are both legitimate.
double LengthTolerance(double scale);

} // namespace cadgeom::geom
