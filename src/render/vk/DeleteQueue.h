// Deferred destruction. A resource the GPU may still be reading cannot be freed
// the moment the CPU stops referring to it, and the cheapest correct answer is
// to hold it for a few frames (docs/architecture.md §4.1).
//
// The clock is the engine's frame counter, bumped once per Tick(). A resource
// retired during frame N is destroyed once frame N + kRetentionFrames starts,
// by which point every submission that could have referenced it has been waited
// on — the frame loop waits on the fence two slots back before reusing a slot,
// and a tick that submits nothing only lets the GPU drain further.
#pragma once

#include <stdint.h>

#include <functional>
#include <utility>
#include <vector>

namespace cadgeom::render::vk {

class DeleteQueue {
public:
    /// Frames in flight (2) plus one for the tick that retired the resource.
    static constexpr uint64_t kRetentionFrames = 3;

    void Push(uint64_t currentFrame, std::function<void()> destroy) {
        pending_.push_back({currentFrame + kRetentionFrames, std::move(destroy)});
    }

    void Flush(uint64_t currentFrame) {
        size_t keep = 0;
        for (size_t i = 0; i < pending_.size(); ++i) {
            if (pending_[i].dueFrame <= currentFrame) {
                pending_[i].destroy();
            } else {
                if (keep != i) {
                    pending_[keep] = std::move(pending_[i]);
                }
                ++keep;
            }
        }
        pending_.resize(keep);
    }

    /// Runs everything regardless of due date. The caller is responsible for
    /// having idled the device first.
    void FlushAll() {
        for (Entry& entry : pending_) {
            entry.destroy();
        }
        pending_.clear();
    }

    bool IsEmpty() const { return pending_.empty(); }

private:
    struct Entry {
        uint64_t dueFrame;
        std::function<void()> destroy;
    };

    std::vector<Entry> pending_;
};

} // namespace cadgeom::render::vk
