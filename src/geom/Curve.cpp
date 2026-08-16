#include "geom/Curve.h"

#include <algorithm>

namespace cadgeom::geom {
namespace {

/// Below eight chords a full circle reads as a polygon at any zoom, whatever
/// the tolerance says. Above four thousand the extra vertices are invisible and
/// only cost upload bandwidth.
constexpr uint32_t kMinFullCircleSegments = 8;
constexpr uint32_t kMaxFullCircleSegments = 4096;

/// Fallbacks for a TessParams left at its "engine default" zeros. Matched to a
/// model measured in millimetres, which is what CAD defaults to.
constexpr double kDefaultChordTolerance = 0.01;
constexpr double kDefaultAngularTolerance = 12.0 * kDegToRad;

} // namespace

WorkPlane FrameOf(const Plane& plane) {
    return MakeWorkPlane(plane.origin, plane.normal);
}

uint32_t ArcSegmentCount(double radius, double sweepAngle, const TessParams& tess) {
    const double sweep = std::fabs(sweepAngle);
    if (!(sweep > 0.0) || !(radius > 0.0)) {
        return 1;
    }

    const double chordTol =
        tess.chordTolerance > 0.0 ? tess.chordTolerance : kDefaultChordTolerance;
    const double angularTol =
        tess.angularTolerance > 0.0 ? tess.angularTolerance : kDefaultAngularTolerance;

    // Sagitta: a chord subtending theta on radius r deviates from the arc by
    // r * (1 - cos(theta/2)) at its midpoint. Inverting that for theta is what
    // makes the segment count scale with the radius instead of being guessed.
    double maxAngle = angularTol;
    if (chordTol < radius) {
        maxAngle = std::min(maxAngle, 2.0 * std::acos(1.0 - chordTol / radius));
    }
    // A tolerance looser than the radius itself still gets a sane lower bound
    // rather than a single chord across the whole sweep.
    maxAngle = std::min(maxAngle, kTwoPi / kMinFullCircleSegments);

    const double exact = std::ceil(sweep / maxAngle);
    // Scale the clamps by how much of a full turn this sweep covers, so a
    // quarter arc is not held to a full circle's minimum.
    const double fraction = sweep / kTwoPi;
    const auto lower = static_cast<double>(kMinFullCircleSegments) * fraction;
    const auto upper = static_cast<double>(kMaxFullCircleSegments) * fraction;
    const double clamped = std::clamp(exact, std::max(1.0, std::ceil(lower)), std::max(1.0, upper));
    return static_cast<uint32_t>(clamped);
}

void SampleArc(const Plane& plane, double radius, double startAngle, double sweepAngle,
               uint32_t segmentCount, bool dropLastPoint, std::vector<Vec3d>& out) {
    const WorkPlane frame = FrameOf(plane);
    const uint32_t segments = std::max(segmentCount, 1u);
    const uint32_t points = dropLastPoint ? segments : segments + 1;

    out.reserve(out.size() + points);
    const double step = sweepAngle / static_cast<double>(segments);
    for (uint32_t i = 0; i < points; ++i) {
        const double angle = startAngle + step * static_cast<double>(i);
        out.push_back(frame.origin + frame.uAxis * (radius * std::cos(angle)) +
                      frame.vAxis * (radius * std::sin(angle)));
    }
}

void RectangleCorners(const Plane& plane, const Vec3d& uAxis, double width, double height,
                      Vec3d out[4]) {
    const WorkPlane frame = MakeWorkPlane(plane.origin, plane.normal, uAxis);
    const Vec3d halfU = frame.uAxis * (0.5 * width);
    const Vec3d halfV = frame.vAxis * (0.5 * height);

    // Counter-clockwise seen from the +normal side, so the winding matches the
    // arc sampling above and a profile's orientation is one rule, not two.
    out[0] = frame.origin - halfU - halfV;
    out[1] = frame.origin + halfU - halfV;
    out[2] = frame.origin + halfU + halfV;
    out[3] = frame.origin - halfU + halfV;
}

bool IsUsablePlane(const Plane& plane) {
    return LengthSq(plane.normal) > kEpsilon;
}

double LengthTolerance(double scale) {
    // 1e-12 of the working scale, floored so a shape sitting at the origin still
    // gets a usable epsilon.
    return std::max(1e-12 * std::fabs(scale), kEpsilon);
}

} // namespace cadgeom::geom
