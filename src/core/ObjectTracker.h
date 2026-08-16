// Live-object accounting for every interface implementation in the module.
//
// M0's acceptance criterion is "create/release the engine, no leaks", and the
// cheapest way to actually hold that line is to make leaks countable: each impl
// object embeds a tracker, and the test suite asserts the count is back to zero
// after Release(). This costs one atomic increment per object and catches the
// exact class of bug that COM-style manual lifetimes invite.
#pragma once

#include <stdint.h>

namespace cadgeom::core {

/// Number of tracked objects currently alive in this module.
uint64_t LiveObjectCount() noexcept;

/// Total constructed since load. Useful for spotting churn in a soak test.
uint64_t TotalObjectsCreated() noexcept;

/// Embed as a member in every interface implementation. Non-copyable so a
/// careless copy cannot make the books disagree.
class ObjectTracker {
public:
    ObjectTracker() noexcept;
    ~ObjectTracker() noexcept;

    ObjectTracker(const ObjectTracker&) = delete;
    ObjectTracker& operator=(const ObjectTracker&) = delete;
    ObjectTracker(ObjectTracker&&) = delete;
    ObjectTracker& operator=(ObjectTracker&&) = delete;
};

} // namespace cadgeom::core
