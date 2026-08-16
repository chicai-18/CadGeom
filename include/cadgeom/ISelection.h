// CadGeom — selection set.
#ifndef CADGEOM_ISELECTION_H
#define CADGEOM_ISELECTION_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

/// Owned by the scene. Enumeration is index-based so no array ever has to be
/// marshalled across the boundary; the order is unspecified but stable between
/// mutations.
class CADGEOM_API ISelection {
public:
    virtual void Clear() = 0;
    virtual void Add(EntityId entity) = 0;
    virtual void Remove(EntityId entity) = 0;
    virtual void Toggle(EntityId entity) = 0;
    virtual void Set(CgSpan<const EntityId> entities) = 0;

    virtual bool Contains(EntityId entity) const = 0;
    virtual uint32_t GetCount() const = 0;
    virtual EntityId GetAt(uint32_t index) const = 0;

    /// Sub-element selection, valid only when exactly one entity is selected.
    /// `kind` of PickKind::None means the whole entity is selected.
    virtual void SetSubElement(PickKind kind, uint32_t subIndex) = 0;
    virtual PickKind GetSubElementKind() const = 0;
    virtual uint32_t GetSubElementIndex() const = 0;

    /// Hover is separate from selection: it drives the pre-highlight the user
    /// sees before committing to a click.
    virtual void SetHovered(EntityId entity) = 0;
    virtual EntityId GetHovered() const = 0;

    /// Combined bounds of the selection. False when the selection is empty.
    virtual bool GetBounds(Aabb& out) const = 0;

protected:
    virtual ~ISelection() = default;
};

} // namespace cadgeom

#endif // CADGEOM_ISELECTION_H
