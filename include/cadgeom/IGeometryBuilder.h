// CadGeom — parametric geometry creation.
//
// Every call here creates an entity whose geometry is a *parametric*
// definition, not a triangle list. Changing a circle's radius regenerates the
// mesh; it never edits triangles (docs/architecture.md §3.1). The parameters
// are the single source of truth and the mesh is a derived cache.
#ifndef CADGEOM_IGEOMETRYBUILDER_H
#define CADGEOM_IGEOMETRYBUILDER_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

/// Parametric definition of one shape. The active member is selected by
/// ShapeType; a union keeps this POD and cheap to pass by pointer.
struct ShapeParams {
    struct PointParams {
        Vec3d position;
    };
    struct LineParams {
        Vec3d start;
        Vec3d end;
    };
    struct CircleParams {
        Plane plane;  ///< origin = center, normal = axis
        double radius;
    };
    struct ArcParams {
        Plane plane;
        double radius;
        double startAngle;  ///< Radians, measured from the plane's reference axis.
        double sweepAngle;  ///< Radians, signed; CCW about the normal is positive.
    };
    struct RectangleParams {
        Plane plane;      ///< origin = center
        Vec3d uAxis;      ///< In-plane direction of `width`.
        double width;
        double height;
    };
    struct ExtrudeParams {
        /// 拉伸的出处。**只是出处，不是引用**：实体带着轮廓定义的一份自己的拷贝，
        /// 所以轮廓那个实体被删掉之后这个 id 会失效，而实体照样能改高度、重新生成。
        /// SetParams 也因此不接受换掉它 —— 换轮廓等于重新拉伸一次。
        ShapeId profile;
        /// 扫掠方向，**轮廓所在实体的对象空间**。IGeometryBuilder::Extrude 收的是
        /// 世界方向并在那里折算一次；这里读到的是折算之后的结果。
        Vec3d direction;
        double distance;
        ExtrudeOptions options;
    };

    ShapeType type{ShapeType::None};
    // `point` carries the initializer only so the union has a default
    // constructor at all: ExtrudeParams embeds ExtrudeOptions, which has
    // in-class initializers, and a union with such a variant member is
    // default-deleted unless some member is initialized here.
    union {
        PointParams point{};
        LineParams line;
        CircleParams circle;
        ArcParams arc;
        RectangleParams rectangle;
        ExtrudeParams extrude;
    };
};

/// Creates entities in the scene that owns this builder. Every method pushes a
/// command, so everything built here is undoable.
///
/// All of these return kInvalidEntity on failure; ask the engine for
/// GetLastError()/GetLastErrorMessage() for the reason.
class CADGEOM_API IGeometryBuilder {
public:
    virtual EntityId MakePoint(const Vec3d& position) = 0;
    virtual EntityId MakeLine(const Vec3d& start, const Vec3d& end) = 0;
    virtual EntityId MakeCircle(const Plane& plane, double radius) = 0;
    virtual EntityId MakeArc(const Plane& plane, double radius, double startAngle,
                             double sweepAngle) = 0;
    virtual EntityId MakeRectangle(const Plane& plane, const Vec3d& uAxis, double width,
                                   double height) = 0;
    virtual EntityId MakePolyline(CgSpan<const Vec3d> points, bool closed) = 0;

    /// @brief 把一个闭合轮廓扫掠成实体，连同拓扑（面 / 边 / 顶点）一起 —— 面拾取
    ///        和后期的布尔运算都要它。
    /// @param profile   闭合轮廓：圆、整圈的圆弧、矩形，或者闭合的多段线。
    /// @param direction 扫掠方向（世界空间），不必归一化；不能落在轮廓平面里。
    /// @param distance  扫掠距离，按轮廓自身的单位算，可以为负。
    /// @param options   拔模角 / 双向 / 是否封端。
    /// @return 新实体的 id；失败时返回 kInvalidEntity，原因见 GetLastErrorMessage()。
    /// @note 轮廓留在场景里，不会被吃掉 —— 它就是那张贴在实体底面上的草图，删不删
    ///       由宿主决定。新实体与轮廓同父、同局部变换，因此正好落在轮廓上。
    /// @note 可撤销 —— 内部压入 ICommand，调用方无需自行入栈。
    virtual EntityId Extrude(EntityId profile, const Vec3d& direction, double distance,
                             const ExtrudeOptions& options) = 0;

    /// Reads back the parametric definition. False if the entity carries no
    /// parametric shape (an imported mesh, for instance).
    virtual bool GetParams(EntityId entity, ShapeParams& out) const = 0;

    /// Edits a shape in place: marks the mesh cache dirty and re-tessellates
    /// before the next frame. `type` must match the existing shape's type.
    virtual CgResult SetParams(EntityId entity, const ShapeParams& params) = 0;

    /// Global tessellation quality. Changing it invalidates every mesh cache.
    virtual void SetTessParams(const TessParams& params) = 0;
    virtual void GetTessParams(TessParams& out) const = 0;

protected:
    virtual ~IGeometryBuilder() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IGEOMETRYBUILDER_H
