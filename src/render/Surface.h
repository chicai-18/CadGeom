// The window abstraction (docs/architecture.md §4.4).
//
// Three backends behind one interface:
//   Glfw    — the engine owns the window and the message loop. Demos, tools,
//             anything that wants a 3D view without writing platform code.
//   Native  — the host owns the window; the engine is handed a raw handle and
//             never touches the OS event queue. This is the embedding mode.
//   Headless— no presentation at all. Renders off-screen, which is what makes
//             screenshots and image-based tests possible without a display.
//
// Input arrives here only in the Glfw case; a native surface stays silent
// because its host forwards events through IViewport::OnMouseEvent directly.
#pragma once

#include "render/vk/Common.h"

#include <functional>
#include <memory>

namespace cadgeom::render {

/// Where a surface sends what it observes. Every field may be empty.
struct SurfaceCallbacks {
    std::function<void(const MouseEvent&)> onMouse;
    std::function<void(const KeyEvent&)> onKey;
    /// Framebuffer size, in pixels — not window size, which differs on HiDPI.
    std::function<void(uint32_t width, uint32_t height)> onResize;
};

class Surface {
public:
    virtual ~Surface() = default;

    /// Creates the presentation surface. Headless surfaces succeed and leave
    /// `out` null: "nothing to present to" is a configuration, not a failure.
    virtual CgResult CreateVkSurface(VkInstance instance, VkSurfaceKHR& out) = 0;

    virtual void GetExtent(uint32_t& width, uint32_t& height) const = 0;

    /// Tells the surface what size the host says it now is. A GLFW window
    /// ignores this — the window system is the authority there — but a headless
    /// surface has no other way to learn it, and a native one uses it as the
    /// fallback for when the platform query fails.
    virtual void SetExtent(uint32_t width, uint32_t height) { (void)width; (void)height; }

    virtual bool IsMinimized() const = 0;

    /// True once the user has asked the window to close. Always false for
    /// native and headless surfaces — that decision belongs to the host.
    virtual bool ShouldClose() const { return false; }

    /// False for headless surfaces, which never create a swapchain.
    virtual bool PresentsToScreen() const { return true; }

    virtual void SetCallbacks(SurfaceCallbacks callbacks) { callbacks_ = std::move(callbacks); }

protected:
    SurfaceCallbacks callbacks_;
};

/// Null on failure, with the reason in the thread's error slot.
std::unique_ptr<Surface> CreateSurface(const SurfaceDesc& desc);

/// Drains the window system's event queue for engine-owned windows. Called once
/// per ICadEngine::Tick(); a no-op when the engine owns no windows.
void PumpWindowEvents();

} // namespace cadgeom::render
