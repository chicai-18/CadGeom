// Double-precision math for everything upstream of the GPU.
//
// The kernel, the scene graph and picking all run in double; only the final
// upload to a shader narrows to float, and it does so relative to the camera
// origin so far-from-origin models do not jitter (docs/architecture.md §0.1).
// glm is deliberately not used here — it is float-first and shader-facing.
//
// Internal header. Operators live in namespace `cadgeom` so they are found by
// ADL on the public POD types; this file is never installed.
#pragma once

#include <cadgeom/Types.h>

#include <cmath>
#include <limits>

namespace cadgeom {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;
inline constexpr double kHalfPi = 0.5 * kPi;
inline constexpr double kDegToRad = kPi / 180.0;
inline constexpr double kRadToDeg = 180.0 / kPi;

/// Default geometric tolerance, in model units. Points closer than this are
/// the same point as far as the kernel is concerned.
inline constexpr double kEpsilon = 1e-9;

inline bool NearlyEqual(double a, double b, double eps = kEpsilon) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Vec3d
// ---------------------------------------------------------------------------

inline Vec3d operator+(const Vec3d& a, const Vec3d& b) {
    return Vec3d{a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3d operator-(const Vec3d& a, const Vec3d& b) {
    return Vec3d{a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3d operator-(const Vec3d& v) { return Vec3d{-v.x, -v.y, -v.z}; }
inline Vec3d operator*(const Vec3d& v, double s) {
    return Vec3d{v.x * s, v.y * s, v.z * s};
}
inline Vec3d operator*(double s, const Vec3d& v) { return v * s; }
inline Vec3d operator/(const Vec3d& v, double s) {
    return Vec3d{v.x / s, v.y / s, v.z / s};
}
inline Vec3d& operator+=(Vec3d& a, const Vec3d& b) { a = a + b; return a; }
inline Vec3d& operator-=(Vec3d& a, const Vec3d& b) { a = a - b; return a; }
inline Vec3d& operator*=(Vec3d& a, double s) { a = a * s; return a; }

inline double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3d Cross(const Vec3d& a, const Vec3d& b) {
    return Vec3d{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double LengthSq(const Vec3d& v) { return Dot(v, v); }
inline double Length(const Vec3d& v) { return std::sqrt(Dot(v, v)); }
inline double Distance(const Vec3d& a, const Vec3d& b) { return Length(b - a); }

/// Returns the zero vector for degenerate input rather than NaNs — callers in
/// the interaction layer feed it raw mouse-derived directions.
inline Vec3d Normalized(const Vec3d& v) {
    const double len = Length(v);
    return len > kEpsilon ? v / len : Vec3d{0.0, 0.0, 0.0};
}

/// Any unit vector perpendicular to `v`. Used to build a work plane's uAxis
/// from nothing but a normal.
inline Vec3d Perpendicular(const Vec3d& v) {
    const Vec3d axis = std::fabs(v.z) < 0.9 ? Vec3d{0.0, 0.0, 1.0} : Vec3d{1.0, 0.0, 0.0};
    return Normalized(Cross(axis, v));
}

// ---------------------------------------------------------------------------
// Quatd — unit quaternions only
// ---------------------------------------------------------------------------

inline Quatd QuatIdentity() { return Quatd{0.0, 0.0, 0.0, 1.0}; }

inline Quatd QuatFromAxisAngle(const Vec3d& axis, double radians) {
    const Vec3d n = Normalized(axis);
    const double h = 0.5 * radians;
    const double s = std::sin(h);
    return Quatd{n.x * s, n.y * s, n.z * s, std::cos(h)};
}

/// Hamilton product: `a * b` applies b first, then a.
inline Quatd operator*(const Quatd& a, const Quatd& b) {
    return Quatd{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                 a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                 a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quatd Normalized(const Quatd& q) {
    const double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return len > kEpsilon ? Quatd{q.x / len, q.y / len, q.z / len, q.w / len}
                          : QuatIdentity();
}

inline Vec3d Rotate(const Quatd& q, const Vec3d& v) {
    const Vec3d u{q.x, q.y, q.z};
    const Vec3d t = 2.0 * Cross(u, v);
    return v + q.w * t + Cross(u, t);
}

// ---------------------------------------------------------------------------
// Mat4d — column-major, m[column * 4 + row], matching GLSL
// ---------------------------------------------------------------------------

inline Mat4d Mat4Identity() {
    Mat4d r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
    return r;
}

inline Mat4d operator*(const Mat4d& a, const Mat4d& b) {
    Mat4d r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

/// Full 4x4 transform of a position (w = 1), including the perspective divide
/// when the matrix has one.
inline Vec3d TransformPoint(const Mat4d& m, const Vec3d& p) {
    const double x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    const double y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    const double z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    const double w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    return (w != 0.0 && w != 1.0) ? Vec3d{x / w, y / w, z / w} : Vec3d{x, y, z};
}

/// Direction transform — ignores translation. Not correct for normals under
/// non-uniform scale; use the inverse-transpose for those.
inline Vec3d TransformDirection(const Mat4d& m, const Vec3d& v) {
    return Vec3d{m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
                 m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
                 m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}

inline Mat4d ToMatrix(const Transform& t) {
    const Quatd q = Normalized(t.rotation);
    const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4d r{};
    r.m[0] = (1.0 - 2.0 * (yy + zz)) * t.scale.x;
    r.m[1] = (2.0 * (xy + wz)) * t.scale.x;
    r.m[2] = (2.0 * (xz - wy)) * t.scale.x;
    r.m[3] = 0.0;

    r.m[4] = (2.0 * (xy - wz)) * t.scale.y;
    r.m[5] = (1.0 - 2.0 * (xx + zz)) * t.scale.y;
    r.m[6] = (2.0 * (yz + wx)) * t.scale.y;
    r.m[7] = 0.0;

    r.m[8] = (2.0 * (xz + wy)) * t.scale.z;
    r.m[9] = (2.0 * (yz - wx)) * t.scale.z;
    r.m[10] = (1.0 - 2.0 * (xx + yy)) * t.scale.z;
    r.m[11] = 0.0;

    r.m[12] = t.translation.x;
    r.m[13] = t.translation.y;
    r.m[14] = t.translation.z;
    r.m[15] = 1.0;
    return r;
}

// ---------------------------------------------------------------------------
// Aabb
// ---------------------------------------------------------------------------

/// Inverted bounds — the identity for Expand(), so accumulating over an empty
/// range yields something IsEmpty() reports honestly.
inline Aabb AabbEmpty() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    return Aabb{Vec3d{inf, inf, inf}, Vec3d{-inf, -inf, -inf}};
}

inline bool IsEmpty(const Aabb& b) {
    return b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z;
}

inline void Expand(Aabb& b, const Vec3d& p) {
    if (p.x < b.min.x) b.min.x = p.x;
    if (p.y < b.min.y) b.min.y = p.y;
    if (p.z < b.min.z) b.min.z = p.z;
    if (p.x > b.max.x) b.max.x = p.x;
    if (p.y > b.max.y) b.max.y = p.y;
    if (p.z > b.max.z) b.max.z = p.z;
}

inline void Expand(Aabb& b, const Aabb& other) {
    if (IsEmpty(other)) {
        return;
    }
    Expand(b, other.min);
    Expand(b, other.max);
}

inline Vec3d Center(const Aabb& b) { return (b.min + b.max) * 0.5; }
inline Vec3d Extents(const Aabb& b) { return b.max - b.min; }

inline double DiagonalLength(const Aabb& b) {
    return IsEmpty(b) ? 0.0 : Length(Extents(b));
}

/// Bounds of the transformed box: all eight corners, re-accumulated. Cheaper
/// tricks exist, but this one is correct under any transform including shear.
inline Aabb Transformed(const Aabb& b, const Mat4d& m) {
    if (IsEmpty(b)) {
        return b;
    }
    Aabb r = AabbEmpty();
    for (int i = 0; i < 8; ++i) {
        const Vec3d corner{(i & 1) ? b.max.x : b.min.x,
                           (i & 2) ? b.max.y : b.min.y,
                           (i & 4) ? b.max.z : b.min.z};
        Expand(r, TransformPoint(m, corner));
    }
    return r;
}

// ---------------------------------------------------------------------------
// Plane / WorkPlane / Ray
// ---------------------------------------------------------------------------

/// Builds an orthonormal frame on a plane. `preferredU` seeds the in-plane
/// axis; anything parallel to the normal falls back to an arbitrary choice.
inline WorkPlane MakeWorkPlane(const Vec3d& origin, const Vec3d& normal,
                               const Vec3d& preferredU = Vec3d{0.0, 0.0, 0.0}) {
    WorkPlane wp{};
    wp.origin = origin;
    wp.normal = Normalized(normal);
    if (LengthSq(wp.normal) < kEpsilon) {
        wp.normal = Vec3d{0.0, 0.0, 1.0};
    }

    Vec3d u = preferredU - wp.normal * Dot(preferredU, wp.normal);
    if (LengthSq(u) < kEpsilon) {
        u = Perpendicular(wp.normal);
    }
    wp.uAxis = Normalized(u);
    wp.vAxis = Cross(wp.normal, wp.uAxis);
    return wp;
}

/// The XY plane at the origin — the default drawing plane in a Z-up world.
inline WorkPlane DefaultWorkPlane() {
    return WorkPlane{Vec3d{0.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 1.0}, Vec3d{1.0, 0.0, 0.0},
                     Vec3d{0.0, 1.0, 0.0}};
}

// MSVC's optimizer flags the guarded division below as a potential divide by
// zero whenever it inlines a call with a literally parallel ray: it folds the
// denominator to 0 and warns without re-proving that the early return already
// fired. C4723 is emitted during code generation, so the pragma has to wrap the
// whole function rather than the expression.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4723)
#endif

/// Ray/plane intersection. False when the ray is parallel to the plane or the
/// hit is behind the origin.
inline bool RayPlaneIntersect(const Ray& ray, const Vec3d& planeOrigin,
                              const Vec3d& planeNormal, Vec3d& out) {
    const double denom = Dot(ray.dir, planeNormal);
    if (std::fabs(denom) < kEpsilon) {
        return false;
    }
    const double t = Dot(planeOrigin - ray.origin, planeNormal) / denom;
    if (t < 0.0) {
        return false;
    }
    out = ray.origin + ray.dir * t;
    return true;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace cadgeom
