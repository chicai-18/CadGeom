#include "geom/Intersect.h"

#include <algorithm>

namespace cadgeom::geom {
namespace {

/// 方向分量小于这个值就当成与该轴的 slab 平行处理，避免除出 inf 后再相减得到 NaN。
constexpr double kParallel = 1e-12;

double ClampUnit(double v) {
    return std::clamp(v, 0.0, 1.0);
}

} // namespace

bool RayAabb(const Ray& ray, const Aabb& box, double& tNear, double& tFar) {
    if (IsEmpty(box)) {
        return false;
    }

    double near = -std::numeric_limits<double>::infinity();
    double far = std::numeric_limits<double>::infinity();

    const double origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const double dir[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
    const double lo[3] = {box.min.x, box.min.y, box.min.z};
    const double hi[3] = {box.max.x, box.max.y, box.max.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < kParallel) {
            // 平行于这一对 slab：要么整条射线在里面，要么整条在外面。
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return false;
            }
            continue;
        }
        double t1 = (lo[axis] - origin[axis]) / dir[axis];
        double t2 = (hi[axis] - origin[axis]) / dir[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        near = std::max(near, t1);
        far = std::min(far, t2);
        if (near > far) {
            return false;
        }
    }

    if (far < 0.0) {
        return false;  // 整个盒子都在射线起点后面。
    }
    tNear = near;
    tFar = far;
    return true;
}

bool RayTriangle(const Ray& ray, const Vec3d& a, const Vec3d& b, const Vec3d& c, double& t) {
    const Vec3d edge1 = b - a;
    const Vec3d edge2 = c - a;
    const Vec3d pvec = Cross(ray.dir, edge2);
    const double det = Dot(edge1, pvec);
    // 只排除退化三角形和与三角形平面平行的射线，不排除背面。
    if (std::fabs(det) < kParallel) {
        return false;
    }

    const double invDet = 1.0 / det;
    const Vec3d tvec = ray.origin - a;
    const double u = Dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }

    const Vec3d qvec = Cross(tvec, edge1);
    const double v = Dot(ray.dir, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }

    const double hit = Dot(edge2, qvec) * invDet;
    if (hit < 0.0) {
        return false;
    }
    t = hit;
    return true;
}

void ClosestRaySegment(const Ray& ray, const Vec3d& a, const Vec3d& b, double& rayT, double& segT,
                       double& distance) {
    const Vec3d seg = b - a;
    const double segLenSq = LengthSq(seg);
    const Vec3d toStart = ray.origin - a;

    double s = 0.0;  // 线段参数
    if (segLenSq <= kParallel) {
        s = 0.0;  // 退化线段：当一个点处理。
    } else {
        // 最小二乘解，|dir| == 1 已经把 u·u 那一项化掉了。
        const double dirDotSeg = Dot(ray.dir, seg);
        const double denom = segLenSq - dirDotSeg * dirDotSeg;
        if (std::fabs(denom) < kParallel) {
            // 平行：整条线段离射线一样远，取起点，下面的重解会把它落到正确位置。
            s = 0.0;
        } else {
            s = ClampUnit((Dot(seg, toStart) - dirDotSeg * Dot(ray.dir, toStart)) / denom);
        }
    }

    // 钳过之后重新解另一个参数，再钳一次 —— 少了这一步，被端点截断的那一侧会
    // 报出一个偏大的距离，近乎与射线平行的线段就选不中了。
    double t = Dot(ray.dir, (a + seg * s) - ray.origin);
    if (t < 0.0) {
        t = 0.0;
        if (segLenSq > kParallel) {
            s = ClampUnit(Dot(ray.origin - a, seg) / segLenSq);
        }
    }

    rayT = t;
    segT = s;
    distance = Distance(ray.origin + ray.dir * t, a + seg * s);
}

double RayPointDistance(const Ray& ray, const Vec3d& p, double& rayT) {
    double t = Dot(p - ray.origin, ray.dir);
    if (t < 0.0) {
        t = 0.0;
    }
    rayT = t;
    return Distance(ray.origin + ray.dir * t, p);
}

bool ClosestLineLine(const Vec3d& p0, const Vec3d& d0, const Vec3d& p1, const Vec3d& d1, double& s0,
                     double& s1) {
    const double a = Dot(d0, d0);
    const double b = Dot(d0, d1);
    const double c = Dot(d1, d1);
    const double denom = a * c - b * b;
    // 相对判据：两条方向都很长时，一个绝对阈值会把明显不平行的组合误判掉。
    if (denom <= kParallel * std::max(1.0, a * c)) {
        return false;
    }

    const Vec3d w = p0 - p1;
    const double d = Dot(d0, w);
    const double e = Dot(d1, w);
    s0 = (b * e - c * d) / denom;
    s1 = (a * e - b * d) / denom;
    return true;
}

void ClosestSegmentSegment(const Vec3d& a0, const Vec3d& a1, const Vec3d& b0, const Vec3d& b1,
                           Vec3d& point, double& distance) {
    const Vec3d u = a1 - a0;
    const Vec3d v = b1 - b0;
    const Vec3d w = a0 - b0;
    const double a = Dot(u, u);
    const double b = Dot(u, v);
    const double c = Dot(v, v);
    const double d = Dot(u, w);
    const double e = Dot(v, w);
    const double denom = a * c - b * b;

    double s = 0.0;
    double t = 0.0;
    if (denom > kParallel * std::max(1.0, a * c)) {
        s = ClampUnit((b * e - c * d) / denom);
    } else if (a > kParallel) {
        s = 0.0;  // 平行：任取一端，下面的重解会把它落到正确位置。
    }

    if (c > kParallel) {
        t = ClampUnit((b * s + e) / c);
        // 重解 s，否则一端被钳住时得到的不是真正的最近点对。
        if (a > kParallel) {
            s = ClampUnit((b * t - d) / a);
        }
    }

    const Vec3d pa = a0 + u * s;
    const Vec3d pb = b0 + v * t;
    point = (pa + pb) * 0.5;
    distance = Distance(pa, pb);
}

} // namespace cadgeom::geom
