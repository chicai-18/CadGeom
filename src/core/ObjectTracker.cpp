#include "core/ObjectTracker.h"

#include <atomic>

namespace cadgeom::core {
namespace {

std::atomic<uint64_t> g_live{0};
std::atomic<uint64_t> g_total{0};

} // namespace

uint64_t LiveObjectCount() noexcept {
    return g_live.load(std::memory_order_relaxed);
}

uint64_t TotalObjectsCreated() noexcept {
    return g_total.load(std::memory_order_relaxed);
}

ObjectTracker::ObjectTracker() noexcept {
    g_live.fetch_add(1, std::memory_order_relaxed);
    g_total.fetch_add(1, std::memory_order_relaxed);
}

ObjectTracker::~ObjectTracker() noexcept {
    g_live.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace cadgeom::core
