// The geometry kernel behind an interface (docs/architecture.md §3.2).
//
// One implementation ships today, `SimpleKernel`: a shape table keyed by
// ShapeId, parametric definitions in, tessellated caches out. The interface
// exists so an OCCT-backed kernel can be dropped in later behind
// `EngineDesc::kernelType` without the scene, the renderer or the tools
// noticing.
//
// This is an internal header, not part of the ABI, so it uses STL freely. It
// knows nothing about scenes, entities or Vulkan — which is exactly what lets
// the tests exercise it without a GPU.
#pragma once

#include <cadgeom/Types.h>

#include "geom/Shape.h"

#include <memory>

namespace cadgeom::geom {

class IGeometryKernel {
public:
    virtual ~IGeometryKernel() = default;

    /// Validates and stores a definition. kInvalidShape on failure, with the
    /// reason in the thread's error slot.
    virtual ShapeId Create(const ShapeDef& def) = 0;

    /// Replaces a definition in place, keeping the id. The type must match the
    /// existing shape's — changing what a shape *is* means making a new one.
    virtual CgResult Update(ShapeId id, const ShapeDef& def) = 0;

    virtual bool GetDef(ShapeId id, ShapeDef& out) const = 0;
    virtual void Destroy(ShapeId id) = 0;
    virtual bool Exists(ShapeId id) const = 0;
    virtual ShapeType TypeOf(ShapeId id) const = 0;

    /// The tessellated form, regenerated first if the definition or the
    /// tolerance has moved since the last call. Null for an unknown id.
    virtual const Shape* Resolve(ShapeId id) = 0;

    /// Object-space bounds. False for an unknown id or a shape that produced no
    /// geometry.
    virtual bool GetBounds(ShapeId id, Aabb& out) = 0;

    /// Global tessellation quality. Setting it invalidates every cache, since
    /// every cache was built against the previous value.
    virtual void SetTessParams(const TessParams& tess) = 0;
    virtual void GetTessParams(TessParams& out) const = 0;

    /// Live shapes. Diagnostics and tests only.
    virtual uint32_t GetShapeCount() const = 0;
};

/// The built-in kernel. `KernelType::Occt` is rejected at engine creation, so
/// this never has to decide what to do about it.
std::unique_ptr<IGeometryKernel> CreateSimpleKernel();

/// Shared validation, exposed so the public builder can reject bad input before
/// it has built a command around it. Ok when the definition is buildable;
/// otherwise the failure is recorded in the thread's error slot.
CgResult ValidateShapeDef(const ShapeDef& def);

} // namespace cadgeom::geom
