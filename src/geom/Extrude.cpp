#include "geom/Extrude.h"

#include "core/Error.h"
#include "geom/Curve.h"

#include <algorithm>

namespace cadgeom::geom {
namespace {

/// 斜接偏移在多尖的角上放弃。两条边夹角越小，斜接点跑得越远；到了这个余弦以下，
/// 一度拔模就能把顶点甩出零件几十倍远。
constexpr double kMinMiterCosine = 0.1;

/// @brief 把闭合环在自己平面内按 `amount` 做斜接偏移，正值向外。
/// @return false 表示某个角尖到斜接解不出来。
bool OffsetLoop(const std::vector<Vec3d>& in, const Vec3d& normal, double amount,
                std::vector<Vec3d>& out) {
    const size_t count = in.size();
    out.resize(count);
    if (std::fabs(amount) <= kEpsilon) {
        out = in;
        return true;
    }

    for (size_t i = 0; i < count; ++i) {
        const Vec3d& previous = in[(i + count - 1) % count];
        const Vec3d& next = in[(i + 1) % count];
        // 逆时针环上，一条边的外法线是 edge × normal。
        const Vec3d n0 = Normalized(Cross(Normalized(in[i] - previous), normal));
        const Vec3d n1 = Normalized(Cross(Normalized(next - in[i]), normal));

        const Vec3d bisector = Normalized(n0 + n1);
        if (LengthSq(bisector) < kEpsilon) {
            return false;  // 两条边恰好折返，没有角平分线可言。
        }
        const double cosHalf = Dot(bisector, n1);
        if (cosHalf < kMinMiterCosine) {
            return false;
        }
        // 除以半角余弦，偏移后的两条边才真的各自平移了 amount。
        out[i] = in[i] + bisector * (amount / cosHalf);
    }

    // 每条边的方向都得保住。收得过头时环会穿过自己再翻出来，而这件事光看有向面积
    // 是看不出来的 —— 一个正方形收成负的还是个正方形，绕向都没变。方向翻了的那条
    // 边才是实打实的证据。
    for (size_t i = 0; i < count; ++i) {
        const size_t next = (i + 1) % count;
        if (Dot(out[next] - out[i], in[next] - in[i]) <= 0.0) {
            return false;
        }
    }
    return true;
}

/// @brief 一段侧面的外法线。
///
/// 直接从四边形自身求，而不是从轮廓边求：拔模让侧壁倾斜，斜着扫掠让它更斜，两种
/// 情况用同一个式子就都对了。
Vec3d SideNormal(const Vec3d& base0, const Vec3d& base1, const Vec3d& top0, const Vec3d& planeNormal,
                 double side) {
    Vec3d n = Cross(base1 - base0, top0 - base0);
    if (LengthSq(n) < kEpsilon) {
        // 这一段被拔模压成了零高度，退回轮廓平面里的外法线。
        n = Cross(base1 - base0, planeNormal);
    }
    return Normalized(n) * side;
}

} // namespace

CgResult BuildSweepLoops(const Profile& profile, const Vec3d& direction, double distance,
                         const ExtrudeOptions& options, SweepLoops& out) {
    if (profile.IsEmpty()) {
        return core::SetError(CgResult::GeometryError,
                              "Extrude: the profile has no closed loop to sweep");
    }

    const Vec3d axis = Normalized(direction);
    if (LengthSq(axis) < kEpsilon) {
        return core::SetError(CgResult::InvalidArgument, "Extrude: direction is zero-length");
    }
    if (!(std::fabs(distance) > LengthTolerance(distance))) {
        return core::SetError(CgResult::InvalidArgument,
                              "Extrude: distance must be non-zero (got %g)", distance);
    }
    if (std::fabs(options.draftAngle) >= kMaxDraftAngle) {
        return core::SetError(CgResult::InvalidArgument,
                              "Extrude: draftAngle %g rad is past the %g rad limit; the walls "
                              "would fold over the profile",
                              options.draftAngle, kMaxDraftAngle);
    }

    const Vec3d normal = profile.plane.normal;
    const Vec3d sweep = axis * distance;
    // 净高度指的是沿轮廓法线走了多远。斜着扫掠时它比 distance 小，扫掠方向躺进
    // 轮廓平面时它是零 —— 那样扫出来的不是实体，是一张自交的纸。
    const double along = Dot(sweep, normal);
    if (std::fabs(along) <= LengthTolerance(distance)) {
        return core::SetError(CgResult::InvalidArgument,
                              "Extrude: the direction lies in the profile plane, so the sweep has "
                              "no height");
    }
    out.side = along >= 0.0 ? 1.0 : -1.0;

    const Vec3d baseOffset = options.bothDirections ? sweep * -0.5 : Vec3d{0.0, 0.0, 0.0};
    const Vec3d topOffset = options.bothDirections ? sweep * 0.5 : sweep;

    // 拔模按「离开轮廓平面走了多远」张开，两端都朝外 —— 双向拉伸时上下各是一个
    // 锥台，这也是 CAD 里拔模的一贯含义。
    const double tanDraft = std::tan(options.draftAngle);
    if (!OffsetLoop(profile.points, normal, tanDraft * std::fabs(Dot(baseOffset, normal)),
                    out.base) ||
        !OffsetLoop(profile.points, normal, tanDraft * std::fabs(Dot(topOffset, normal)), out.top)) {
        return core::SetError(CgResult::GeometryError,
                              "Extrude: the draft angle cannot be mitred through a corner this "
                              "sharp");
    }
    for (Vec3d& p : out.base) {
        p += baseOffset;
    }
    for (Vec3d& p : out.top) {
        p += topOffset;
    }

    // 拔模收得太狠会把环收成一个点再翻过来。两个环的有向面积仍为正，是「还是同一
    // 个轮廓」最便宜的判据。
    if (!(ProfileSignedArea(out.base, normal) > 0.0) ||
        !(ProfileSignedArea(out.top, normal) > 0.0)) {
        return core::SetError(CgResult::GeometryError,
                              "Extrude: the draft angle collapses the profile before the sweep "
                              "ends");
    }
    return CgResult::Ok;
}

bool BuildExtrusion(const Profile& profile, const Vec3d& direction, double distance,
                    const ExtrudeOptions& options, MeshData& mesh, PolylineData& wire,
                    Topology& topo) {
    mesh.Clear();
    wire.Clear();
    topo.Clear();

    SweepLoops loops{};
    if (CgFailed(BuildSweepLoops(profile, direction, distance, options, loops))) {
        return false;  // BuildSweepLoops 已经说明了原因。
    }
    const std::vector<Vec3d>& baseLoop = loops.base;
    const std::vector<Vec3d>& topLoop = loops.top;
    const Vec3d normal = profile.plane.normal;
    const double side = loops.side;

    std::vector<uint32_t> capTriangles;
    if (options.capEnds && !TriangulateProfile(profile, capTriangles)) {
        return false;  // TriangulateProfile 已经说明了原因。
    }

    const auto count = static_cast<uint32_t>(profile.points.size());
    const auto pushVertex = [&mesh](const Vec3d& position, const Vec3d& n) {
        mesh.vertices.push_back(MeshVertex{position, n});
        return static_cast<uint32_t>(mesh.vertices.size() - 1);
    };
    const auto triangleCount = [&mesh]() {
        return static_cast<uint32_t>(mesh.indices.size() / 3);
    };
    /// 四边形按 (a, b, c, d) 的环绕顺序给出，`side` 为负时整体翻面。
    const auto pushQuad = [&mesh, side](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        const uint32_t order[6] = {a, b, c, a, c, d};
        const uint32_t flipped[6] = {a, c, b, a, d, c};
        const uint32_t* source = side > 0.0 ? order : flipped;
        mesh.indices.insert(mesh.indices.end(), source, source + 6);
    };

    // -- 端面 ---------------------------------------------------------------
    //
    // capTriangles 绕轮廓法线逆时针。顶面朝 +side*normal，底面朝 -side*normal，
    // 所以两者之中总有一个要翻绕序，翻哪一个由扫掠朝哪边决定。
    if (options.capEnds) {
        const auto cap = [&](const std::vector<Vec3d>& loop, double facing) {
            const Vec3d faceNormal = normal * facing;
            const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
            for (const Vec3d& p : loop) {
                pushVertex(p, faceNormal);
            }
            Topology::Face face{};
            face.firstTriangle = triangleCount();
            face.normal = faceNormal;
            face.planar = true;
            const bool keepOrder = facing > 0.0;
            for (size_t i = 0; i + 2 < capTriangles.size(); i += 3) {
                mesh.indices.push_back(first + capTriangles[i]);
                mesh.indices.push_back(first + capTriangles[i + (keepOrder ? 1 : 2)]);
                mesh.indices.push_back(first + capTriangles[i + (keepOrder ? 2 : 1)]);
            }
            face.triangleCount = triangleCount() - face.firstTriangle;
            topo.faces.push_back(face);
        };
        cap(baseLoop, -side);
        cap(topLoop, side);
    }

    // -- 侧面 ---------------------------------------------------------------
    if (profile.smooth) {
        // 一个光滑面，法线在相邻两段之间取平均。顶点因此可以整圈共用，圆柱侧面
        // 也就真的是一个面，而不是六十四个各自为政的四边形。
        std::vector<Vec3d> segmentNormals(count);
        for (uint32_t s = 0; s < count; ++s) {
            const uint32_t next = (s + 1) % count;
            segmentNormals[s] = SideNormal(baseLoop[s], baseLoop[next], topLoop[s], normal, side);
        }
        std::vector<Vec3d> vertexNormals(count);
        for (uint32_t i = 0; i < count; ++i) {
            vertexNormals[i] =
                Normalized(segmentNormals[(i + count - 1) % count] + segmentNormals[i]);
        }

        const uint32_t baseFirst = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i < count; ++i) {
            pushVertex(baseLoop[i], vertexNormals[i]);
        }
        const uint32_t topFirst = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i < count; ++i) {
            pushVertex(topLoop[i], vertexNormals[i]);
        }

        Topology::Face face{};
        face.firstTriangle = triangleCount();
        face.planar = false;  // 法线逐顶点插值，整个面没有一个统一的法线可言。
        for (uint32_t s = 0; s < count; ++s) {
            const uint32_t next = (s + 1) % count;
            pushQuad(baseFirst + s, baseFirst + next, topFirst + next, topFirst + s);
        }
        face.triangleCount = triangleCount() - face.firstTriangle;
        topo.faces.push_back(face);
    } else {
        for (uint32_t s = 0; s < count; ++s) {
            const uint32_t next = (s + 1) % count;
            const Vec3d faceNormal =
                SideNormal(baseLoop[s], baseLoop[next], topLoop[s], normal, side);

            Topology::Face face{};
            face.firstTriangle = triangleCount();
            face.normal = faceNormal;
            face.planar = true;
            // 四个顶点各归自己这一段：一个顶点只能带一个法线，而硬角两侧的法线
            // 本来就不一样（geom/MeshData.cpp 里那个盒子当年也是这么摊开的）。
            const uint32_t v0 = pushVertex(baseLoop[s], faceNormal);
            const uint32_t v1 = pushVertex(baseLoop[next], faceNormal);
            const uint32_t v2 = pushVertex(topLoop[next], faceNormal);
            const uint32_t v3 = pushVertex(topLoop[s], faceNormal);
            pushQuad(v0, v1, v2, v3);
            face.triangleCount = triangleCount() - face.firstTriangle;
            topo.faces.push_back(face);
        }
    }

    // -- 特征边 -------------------------------------------------------------
    //
    // 底环和顶环永远画：封了端它们是面的轮廓，没封端它们是壳的自由边界，两种情况
    // 下都是零件上看得见的线。
    wire.positions.reserve(static_cast<size_t>(count) * 2);
    wire.positions.insert(wire.positions.end(), baseLoop.begin(), baseLoop.end());
    wire.positions.insert(wire.positions.end(), topLoop.begin(), topLoop.end());
    wire.AddChain(0, count, /*closed=*/true, 0.0);
    topo.edges.push_back(Topology::Edge{0, count});
    wire.AddChain(count, count, /*closed=*/true, wire.length);
    topo.edges.push_back(Topology::Edge{count, count});

    if (!profile.smooth) {
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t firstSegment = wire.SegmentCount();
            wire.AddSegment(i, count + i);
            topo.edges.push_back(Topology::Edge{firstSegment, 1});
        }
        topo.vertices.reserve(static_cast<size_t>(count) * 2);
        for (uint32_t i = 0; i < count * 2; ++i) {
            topo.vertices.push_back(i);
        }
    }

    mesh.RecomputeBounds();
    wire.RecomputeBounds();
    return true;
}

} // namespace cadgeom::geom
