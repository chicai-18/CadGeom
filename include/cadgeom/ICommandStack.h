// CadGeom — undo/redo.
//
// Every scene mutation goes through a command (docs/architecture.md §5).
// Undo is not a feature to bolt on later in a CAD tool; retrofitting it costs
// roughly an order of magnitude more than starting with it.
#ifndef CADGEOM_ICOMMANDSTACK_H
#define CADGEOM_ICOMMANDSTACK_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class IScene;

/// Host-implementable command. Implement this to make an application-level
/// edit participate in the same undo stack as the engine's own operations.
///
/// The engine calls Release() when the command falls off the stack — that is
/// the one place a host-allocated object is destroyed by host code, so the
/// allocation and the free stay on the same side of the boundary.
class CADGEOM_API ICommand {
public:
    virtual void Release() = 0;

    /// Return non-Ok to abort: a failed Execute() must leave the scene
    /// untouched and will not be pushed onto the stack.
    virtual CgResult Execute(IScene* scene) = 0;
    virtual CgResult Undo(IScene* scene) = 0;

    /// UTF-8 label for menus ("Undo Extrude"). Must outlive the command.
    virtual const char* GetName() const = 0;

protected:
    virtual ~ICommand() = default;
};

class CADGEOM_API ICommandStack {
public:
    /// Executes the command and takes ownership. On failure the command is
    /// released immediately and the stack is unchanged.
    virtual CgResult Push(ICommand* command) = 0;

    virtual bool CanUndo() const = 0;
    virtual bool CanRedo() const = 0;
    virtual CgResult Undo() = 0;
    virtual CgResult Redo() = 0;

    /// Label of the next Undo/Redo target, or null when there is none.
    virtual const char* PeekUndoName() const = 0;
    virtual const char* PeekRedoName() const = 0;

    /// Collapse everything pushed until EndGroup() into one undo step, so a
    /// gizmo drag or a multi-entity edit undoes as a single user action.
    /// Groups may nest; only the outermost one produces a stack entry.
    virtual void BeginGroup(const char* utf8Name) = 0;
    virtual void EndGroup() = 0;

    virtual void Clear() = 0;
    virtual uint32_t GetUndoCount() const = 0;
    virtual uint32_t GetRedoCount() const = 0;

    /// 0 = unlimited. Trimming drops the oldest entries first.
    virtual void SetCapacity(uint32_t maxEntries) = 0;

protected:
    virtual ~ICommandStack() = default;
};

} // namespace cadgeom

#endif // CADGEOM_ICOMMANDSTACK_H
