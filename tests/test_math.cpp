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

TEST_CASE("a look-at matrix puts the eye at the origin looking down -Z", "[math][camera]") {
    const Vec3d eye{10.0, -20.0, 15.0};
    const Vec3d target{0.0, 0.0, 5.0};
    const Mat4d view = Mat4LookAt(eye, target, Vec3d{0, 0, 1});

    const Vec3d eyeInView = TransformPoint(view, eye);
    CHECK(eyeInView.x == Approx(0.0).margin(1e-12));
    CHECK(eyeInView.y == Approx(0.0).margin(1e-12));
    CHECK(eyeInView.z == Approx(0.0).margin(1e-12));

    // View space is right-handed with the camera looking along -Z, so the
    // target lands on the negative Z axis at exactly its distance.
    const Vec3d targetInView = TransformPoint(view, target);
    CHECK(targetInView.x == Approx(0.0).margin(1e-12));
    CHECK(targetInView.y == Approx(0.0).margin(1e-12));
    CHECK(targetInView.z == Approx(-Distance(eye, target)));
}

TEST_CASE("projections map onto Vulkan clip space", "[math][camera]") {
    // Vulkan, not OpenGL: depth runs 0..1 rather than -1..1, and +Y points
    // down. Getting either wrong renders an upside-down or z-inverted scene
    // that still looks plausible in a screenshot.
    SECTION("orthographic") {
        const Mat4d p = Mat4Ortho(-2.0, 2.0, -1.0, 1.0, 1.0, 101.0);

        CHECK(TransformPoint(p, Vec3d{0.0, 0.0, -1.0}).z == Approx(0.0).margin(1e-12));
        CHECK(TransformPoint(p, Vec3d{0.0, 0.0, -101.0}).z == Approx(1.0));

        // World +Y maps to clip -Y.
        CHECK(TransformPoint(p, Vec3d{0.0, 1.0, -50.0}).y == Approx(-1.0));
        CHECK(TransformPoint(p, Vec3d{2.0, 0.0, -50.0}).x == Approx(1.0));
    }

    SECTION("perspective") {
        const Mat4d p = Mat4Perspective(60.0 * kDegToRad, 16.0 / 9.0, 0.5, 500.0);

        // TransformPoint divides by w, so these are already NDC.
        CHECK(TransformPoint(p, Vec3d{0.0, 0.0, -0.5}).z == Approx(0.0).margin(1e-9));
        CHECK(TransformPoint(p, Vec3d{0.0, 0.0, -500.0}).z == Approx(1.0));
        CHECK(TransformPoint(p, Vec3d{0.0, 1.0, -10.0}).y < 0.0);

        // The w a point behind the eye comes back with is what WorldToScreen
        // tests to reject it.
        const Vec4d behind = TransformVec4(p, Vec4d{0.0, 0.0, 10.0, 1.0});
        CHECK(behind.w < 0.0);
    }
}

TEST_CASE("inverting a view-projection round-trips a point", "[math][camera]") {
    // This is the identity ScreenToRay depends on: unprojecting the corners of
    // a pixel has to land back on the ray that projected onto it.
    const Mat4d view = Mat4LookAt(Vec3d{30.0, -40.0, 25.0}, Vec3d{1.0, 2.0, 3.0}, Vec3d{0, 0, 1});
    const Mat4d viewProj = Mat4Perspective(45.0 * kDegToRad, 4.0 / 3.0, 0.1, 1000.0) * view;

    Mat4d inverse{};
    REQUIRE(Invert(viewProj, inverse));

    const Vec3d world{7.0, -3.0, 12.0};
    const Vec3d roundTrip = TransformPoint(inverse, TransformPoint(viewProj, world));
    CHECK(roundTrip.x == Approx(world.x).margin(1e-9));
    CHECK(roundTrip.y == Approx(world.y).margin(1e-9));
    CHECK(roundTrip.z == Approx(world.z).margin(1e-9));

    const Mat4d identity = viewProj * inverse;
    for (int i = 0; i < 16; ++i) {
        CHECK(identity.m[i] == Approx(Mat4Identity().m[i]).margin(1e-9));
    }
}

TEST_CASE("a singular matrix reports failure instead of returning NaNs", "[math][camera]") {
    Mat4d singular{};  // All zeroes.
    Mat4d out = Mat4Identity();
    CHECK_FALSE(Invert(singular, out));

    // The convenience form falls back to identity rather than poisoning
    // whatever it is multiplied into.
    const Mat4d fallback = Inverse(singular);
    CHECK(fallback.m[0] == Approx(1.0));
    CHECK(fallback.m[5] == Approx(1.0));
}

TEST_CASE("camera-relative rendering keeps far-from-origin precision", "[math][camera]") {
    // The whole reason viewProj is built with the eye at the origin: the same
    // view a million units out, done absolutely, loses the millimetre.
    const Vec3d cameraOrigin{1'000'000.0, 500'000.0, 300.0};
    const Vec3d point = cameraOrigin + Vec3d{10.0, 20.0, 0.001};
    const double nudge = 0.001;

    // Narrowed where it stands, a millimetre of movement disappears: one float
    // ULP at 1e6 is about 0.06 units.
    CHECK(static_cast<float>(point.x) == static_cast<float>(point.x + nudge));

    // Narrowed after subtracting the camera origin in double, it survives.
    CHECK(static_cast<float>(point.x - cameraOrigin.x) !=
          static_cast<float>(point.x + nudge - cameraOrigin.x));

    // And the relative view matrix has no huge translation to cancel out.
    const Mat4d relativeView =
        Mat4LookAt(Vec3d{0, 0, 0}, Vec3d{10.0, 20.0, -5.0}, Vec3d{0, 0, 1});
    CHECK(std::fabs(relativeView.m[12]) < 1e-12);
    CHECK(std::fabs(relativeView.m[13]) < 1e-12);
    CHECK(std::fabs(relativeView.m[14]) < 1e-12);
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

// ---------------------------------------------------------------------------
// 垂足 —— M6 的 Snap_Perpendicular 靠的就是这两个函数
// ---------------------------------------------------------------------------

TEST_CASE("the foot of a perpendicular onto a segment", "[math][snap]") {
    const Vec3d a{0.0, 0.0, 0.0};
    const Vec3d b{10.0, 0.0, 0.0};

    SECTION("a point above the middle drops straight down") {
        const Vec3d foot = ClosestPointOnSegment(a, b, Vec3d{4.0, 7.0, 0.0});
        CHECK(foot.x == Approx(4.0));
        CHECK(foot.y == Approx(0.0));
        // 垂足的定义：连线与线段正交。
        CHECK(Dot(Vec3d{4.0, 7.0, 0.0} - foot, b - a) == Approx(0.0).margin(1e-12));
    }

    SECTION("beyond the end it clamps to the end") {
        // 延长线上的垂足在图纸上不存在，吸到那儿只会让人莫名其妙。
        CHECK(ClosestPointOnSegment(a, b, Vec3d{25.0, 3.0, 0.0}).x == Approx(10.0));
        CHECK(ClosestPointOnSegment(a, b, Vec3d{-4.0, 3.0, 0.0}).x == Approx(0.0));
    }

    SECTION("without the clamp it lands on the infinite line") {
        CHECK(ClosestPointOnSegment(a, b, Vec3d{25.0, 3.0, 0.0}, false).x == Approx(25.0));
    }

    SECTION("a degenerate segment answers with its own point") {
        CHECK(ClosestPointOnSegment(a, a, Vec3d{5.0, 5.0, 5.0}).x == Approx(0.0));
    }
}

TEST_CASE("the foot of a perpendicular onto a circle", "[math][snap]") {
    const Vec3d centre{0.0, 0.0, 0.0};
    const Vec3d normal{0.0, 0.0, 1.0};
    const double radius = 5.0;

    SECTION("outside the circle, on the ray from the centre") {
        const Vec3d foot = ClosestPointOnCircle(centre, normal, radius, Vec3d{20.0, 0.0, 3.0});
        CHECK(foot.x == Approx(5.0));
        CHECK(foot.y == Approx(0.0));
        // 圆的垂足永远在圆上 —— 它是圆上离参考点最近的那个点。
        CHECK(Distance(foot, centre) == Approx(radius));
        CHECK(foot.z == Approx(0.0));  // 先投到圆所在的平面上。
    }

    SECTION("inside the circle it still lands on the circle") {
        const Vec3d foot = ClosestPointOnCircle(centre, normal, radius, Vec3d{0.0, 1.0, 0.0});
        CHECK(foot.y == Approx(5.0));
        CHECK(Distance(foot, centre) == Approx(radius));
    }

    SECTION("on the axis every direction is equally near, so uAxis decides") {
        // 退化情况得有一个确定的答案，否则光标抖一下结果就换一个。
        const Vec3d foot = ClosestPointOnCircle(centre, normal, radius, Vec3d{0.0, 0.0, 9.0},
                                                Vec3d{0.0, 1.0, 0.0});
        CHECK(foot.y == Approx(5.0));
        CHECK(Distance(foot, centre) == Approx(radius));
    }

    SECTION("a non-positive radius has no circle to land on") {
        CHECK(Distance(ClosestPointOnCircle(centre, normal, 0.0, Vec3d{1.0, 1.0, 1.0}), centre) ==
              Approx(0.0));
    }
}
