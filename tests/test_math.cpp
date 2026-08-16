// Double-precision math, the layer everything above it trusts.
//
// This is the one place the tests reach past the public headers: core/Math.h is
// header-only, so there is nothing to link and nothing to export.

#include "core/Math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace cadgeom;

TEST_CASE("vector algebra", "[math]") {
    const Vec3d a{1.0, 2.0, 3.0};
    const Vec3d b{4.0, 5.0, 6.0};

    CHECK(Dot(a, b) == Approx(32.0));

    const Vec3d c = Cross(a, b);
    CHECK(c.x == Approx(-3.0));
    CHECK(c.y == Approx(6.0));
    CHECK(c.z == Approx(-3.0));

    // Right-handed, Z-up: X cross Y must give +Z, not -Z.
    const Vec3d z = Cross(Vec3d{1, 0, 0}, Vec3d{0, 1, 0});
    CHECK(z.z == Approx(1.0));

    CHECK(Length(Vec3d{3.0, 4.0, 0.0}) == Approx(5.0));
    CHECK(Length(Normalized(Vec3d{0.0, 0.0, 7.0})) == Approx(1.0));
}

TEST_CASE("normalizing a degenerate vector yields zero, not NaN", "[math]") {
    // Mouse-derived directions can collapse to nothing; the interaction layer
    // has to be able to test for that rather than propagate NaNs into a scene.
    const Vec3d n = Normalized(Vec3d{0.0, 0.0, 0.0});
    CHECK(n.x == 0.0);
    CHECK(n.y == 0.0);
    CHECK(n.z == 0.0);
}

TEST_CASE("Perpendicular really is perpendicular, including along Z", "[math]") {
    const Vec3d axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.3, -0.7, 0.64}};
    for (const Vec3d& axis : axes) {
        const Vec3d n = Normalized(axis);
        const Vec3d p = Perpendicular(n);
        CHECK(Length(p) == Approx(1.0));
        CHECK(Dot(p, n) == Approx(0.0).margin(1e-12));
    }
}

TEST_CASE("quaternion rotation", "[math]") {
    const Quatd q = QuatFromAxisAngle(Vec3d{0, 0, 1}, kHalfPi);
    const Vec3d r = Rotate(q, Vec3d{1, 0, 0});

    // 90 degrees about +Z takes +X to +Y in a right-handed frame.
    CHECK(r.x == Approx(0.0).margin(1e-12));
    CHECK(r.y == Approx(1.0));
    CHECK(r.z == Approx(0.0).margin(1e-12));

    // Composition applies the right-hand operand first.
    const Quatd twice = q * q;
    const Vec3d flipped = Rotate(twice, Vec3d{1, 0, 0});
    CHECK(flipped.x == Approx(-1.0));
}

TEST_CASE("matrices are column-major with translation in the last column", "[math]") {
    Transform t{};
    t.translation = Vec3d{10.0, 20.0, 30.0};
    const Mat4d m = ToMatrix(t);

    // GLSL reads m[12..14] as the translation; getting this wrong transposes
    // every upload to the GPU.
    CHECK(m.m[12] == Approx(10.0));
    CHECK(m.m[13] == Approx(20.0));
    CHECK(m.m[14] == Approx(30.0));
    CHECK(m.m[15] == Approx(1.0));

    const Vec3d p = TransformPoint(m, Vec3d{1.0, 1.0, 1.0});
    CHECK(p.x == Approx(11.0));
    CHECK(p.z == Approx(31.0));

    // A direction ignores translation.
    const Vec3d d = TransformDirection(m, Vec3d{1.0, 0.0, 0.0});
    CHECK(d.x == Approx(1.0));
    CHECK(d.y == Approx(0.0));
}

TEST_CASE("transform composition matches parent * child", "[math]") {
    Transform parent{};
    parent.translation = Vec3d{100.0, 0.0, 0.0};
    parent.rotation = QuatFromAxisAngle(Vec3d{0, 0, 1}, kHalfPi);

    Transform child{};
    child.translation = Vec3d{10.0, 0.0, 0.0};

    const Mat4d world = ToMatrix(parent) * ToMatrix(child);
    const Vec3d origin = TransformPoint(world, Vec3d{0, 0, 0});

    // The child's local +X offset is rotated into the parent's +Y.
    CHECK(origin.x == Approx(100.0));
    CHECK(origin.y == Approx(10.0));
}

TEST_CASE("scale is applied before rotation", "[math]") {
    Transform t{};
    t.scale = Vec3d{2.0, 3.0, 4.0};
    t.rotation = QuatFromAxisAngle(Vec3d{0, 0, 1}, kHalfPi);

    const Vec3d p = TransformPoint(ToMatrix(t), Vec3d{1.0, 0.0, 0.0});
    // Scaled to (2,0,0), then rotated onto +Y. Applying them the other way
    // round would give 3.
    CHECK(p.y == Approx(2.0));
}

TEST_CASE("identity transform is the identity", "[math]") {
    const Mat4d m = ToMatrix(Transform{});
    const Mat4d i = Mat4Identity();
    for (int k = 0; k < 16; ++k) {
        CHECK(m.m[k] == Approx(i.m[k]).margin(1e-15));
    }
}

TEST_CASE("empty bounds absorb the first point exactly", "[math]") {
    Aabb b = AabbEmpty();
    CHECK(IsEmpty(b));

    Expand(b, Vec3d{5.0, -2.0, 1.0});
    CHECK_FALSE(IsEmpty(b));
    CHECK(b.min.x == Approx(5.0));
    CHECK(b.max.x == Approx(5.0));

    Expand(b, Vec3d{-5.0, 8.0, 1.0});
    CHECK(b.min.x == Approx(-5.0));
    CHECK(b.max.y == Approx(8.0));
    CHECK(Center(b).x == Approx(0.0));
    CHECK(DiagonalLength(b) > 0.0);
}

TEST_CASE("expanding by empty bounds is a no-op", "[math]") {
    Aabb b = AabbEmpty();
    Expand(b, Vec3d{1.0, 1.0, 1.0});
    Expand(b, AabbEmpty());
    CHECK(b.min.x == Approx(1.0));
    CHECK(b.max.x == Approx(1.0));
}

TEST_CASE("transformed bounds cover the rotated box", "[math]") {
    Aabb b{Vec3d{-1, -1, -1}, Vec3d{1, 1, 1}};

    Transform t{};
    t.rotation = QuatFromAxisAngle(Vec3d{0, 0, 1}, 45.0 * kDegToRad);
    const Aabb r = Transformed(b, ToMatrix(t));

    // A unit cube spun 45 degrees about Z needs sqrt(2) of room in X and Y,
    // and none in Z.
    CHECK(r.max.x == Approx(std::sqrt(2.0)));
    CHECK(r.max.z == Approx(1.0));
}

TEST_CASE("a work plane built from a normal is orthonormal", "[math]") {
    const WorkPlane wp = MakeWorkPlane(Vec3d{1, 2, 3}, Vec3d{0, 0, 5});

    CHECK(Length(wp.normal) == Approx(1.0));
    CHECK(Length(wp.uAxis) == Approx(1.0));
    CHECK(Length(wp.vAxis) == Approx(1.0));
    CHECK(Dot(wp.uAxis, wp.vAxis) == Approx(0.0).margin(1e-12));
    CHECK(Dot(wp.uAxis, wp.normal) == Approx(0.0).margin(1e-12));

    // Right-handed frame: u cross v points along the normal.
    const Vec3d n = Cross(wp.uAxis, wp.vAxis);
    CHECK(Dot(n, wp.normal) == Approx(1.0));
}

TEST_CASE("a preferred in-plane axis is projected, not accepted blindly", "[math]") {
    const WorkPlane wp = MakeWorkPlane(Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, Vec3d{1, 0, 1});
    CHECK(wp.uAxis.z == Approx(0.0).margin(1e-12));
    CHECK(wp.uAxis.x == Approx(1.0));

    // A preferred axis parallel to the normal is unusable; fall back rather
    // than produce a degenerate frame.
    const WorkPlane fallback = MakeWorkPlane(Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, Vec3d{0, 0, 1});
    CHECK(Length(fallback.uAxis) == Approx(1.0));
}

TEST_CASE("the default work plane is XY in a Z-up world", "[math]") {
    const WorkPlane wp = DefaultWorkPlane();
    CHECK(wp.normal.z == Approx(1.0));
    CHECK(wp.uAxis.x == Approx(1.0));
    CHECK(wp.vAxis.y == Approx(1.0));
}

TEST_CASE("ray/plane intersection", "[math]") {
    const Ray down{Vec3d{3.0, 4.0, 10.0}, Vec3d{0.0, 0.0, -1.0}};
    Vec3d hit{};

    REQUIRE(RayPlaneIntersect(down, Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, hit));
    CHECK(hit.x == Approx(3.0));
    CHECK(hit.z == Approx(0.0).margin(1e-12));

    SECTION("a ray parallel to the plane misses") {
        const Ray parallel{Vec3d{0, 0, 5}, Vec3d{1, 0, 0}};
        CHECK_FALSE(RayPlaneIntersect(parallel, Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, hit));
    }

    SECTION("a plane behind the ray misses") {
        const Ray up{Vec3d{0, 0, 10}, Vec3d{0, 0, 1}};
        CHECK_FALSE(RayPlaneIntersect(up, Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, hit));
    }
}

TEST_CASE("double precision survives CAD-scale coordinates", "[math]") {
    // A model placed at a survey coordinate is exactly where float breaks down,
    // and why the kernel is double all the way through.
    const Vec3d origin{6'378'137.0, 1'234'567.0, 250.0};
    const Vec3d nudged = origin + Vec3d{0.001, 0.0, 0.0};

    // One ULP at this magnitude is about 1.4e-9 model units, so a millimetre
    // still resolves with six digits to spare.
    CHECK(nudged.x != origin.x);
    CHECK(Distance(origin, nudged) == Approx(0.001).margin(1e-8));

    // The same arithmetic in float, which is what a naive engine would upload
    // straight to the GPU: one ULP is ~0.5 units, so the nudge disappears
    // entirely. This is the whole argument for camera-relative uploads.
    const float farF = static_cast<float>(origin.x);
    const float nudgedF = farF + 0.001f;
    CHECK(nudgedF == farF);
}
