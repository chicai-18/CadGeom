// The built-in creation tools (docs/architecture.md §6.2).
//
// | tool        | state machine                                          |
// |-------------|--------------------------------------------------------|
// | Select      | click -> pick; Delete removes the selection             |
// | Point       | click -> drop a point on the work plane                 |
// | Line        | point 1 -> rubber band -> point 2 (continuable)         |
// | Circle      | centre -> drag the radius -> confirm                    |
// | Rectangle   | corner 1 -> drag -> corner 2                            |
// | Polyline    | click per vertex; Enter or a double-click finishes      |
//
// Each of them accepts both habits a CAD user arrives with: click-then-click,
// and press-drag-release. The difference is decided by whether the pointer moved
// more than a few pixels between press and release, which is the same rule every
// drawing application converges on.
#pragma once

namespace cadgeom {
class IToolManager;
}

namespace cadgeom::interact {

struct ToolSettings;

/// Creates and registers Select, Point, Line, Circle, Rectangle and Polyline.
/// The manager takes ownership of each. `settings` must outlive the manager —
/// it is the manager's own member.
void RegisterBuiltinTools(IToolManager& manager, ToolSettings& settings);

} // namespace cadgeom::interact
