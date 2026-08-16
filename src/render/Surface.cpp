#include "render/Surface.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace cadgeom::render {
namespace {

// ---------------------------------------------------------------------------
// GLFW lifetime
//
// Reference-counted because several viewports may each own a window, and GLFW
// is a process-wide singleton. Not thread-safe, deliberately: GLFW itself
// requires that every one of these calls happens on the same thread, so a lock
// here would only hide a rule the caller has already broken.
// ---------------------------------------------------------------------------

int g_glfwUsers = 0;

bool AcquireGlfw() {
    if (g_glfwUsers > 0) {
        ++g_glfwUsers;
        return true;
    }
    if (!glfwInit()) {
        const char* description = nullptr;
        glfwGetError(&description);
        core::SetError(CgResult::RenderError, "glfwInit failed: %s",
                       description ? description : "no detail available");
        return false;
    }
    g_glfwUsers = 1;
    CG_DEBUG("GLFW %s initialised", glfwGetVersionString());
    return true;
}

void ReleaseGlfw() {
    if (g_glfwUsers > 0 && --g_glfwUsers == 0) {
        glfwTerminate();
    }
}

uint32_t TranslateMods(int glfwMods) {
    uint32_t mods = KeyMod_None;
    if (glfwMods & GLFW_MOD_SHIFT)   mods |= KeyMod_Shift;
    if (glfwMods & GLFW_MOD_CONTROL) mods |= KeyMod_Ctrl;
    if (glfwMods & GLFW_MOD_ALT)     mods |= KeyMod_Alt;
    if (glfwMods & GLFW_MOD_SUPER)   mods |= KeyMod_Super;
    return mods;
}

MouseButton TranslateButton(int glfwButton) {
    switch (glfwButton) {
        case GLFW_MOUSE_BUTTON_LEFT:   return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT:  return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
        default:                       return MouseButton::None;
    }
}

/// GLFW's key codes line up with cadgeom's Key enum for printable characters
/// and for the first stretch of the navigation block, then diverge at HOME
/// because GLFW has PAGE_UP/PAGE_DOWN in between. Anything with no equivalent
/// reports as Key_Unknown rather than as some other key.
int32_t TranslateKey(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_ESCAPE:    return Key_Escape;
        case GLFW_KEY_ENTER:     return Key_Enter;
        case GLFW_KEY_TAB:       return Key_Tab;
        case GLFW_KEY_BACKSPACE: return Key_Backspace;
        case GLFW_KEY_INSERT:    return Key_Insert;
        case GLFW_KEY_DELETE:    return Key_Delete;
        case GLFW_KEY_RIGHT:     return Key_Right;
        case GLFW_KEY_LEFT:      return Key_Left;
        case GLFW_KEY_DOWN:      return Key_Down;
        case GLFW_KEY_UP:        return Key_Up;
        case GLFW_KEY_HOME:      return Key_Home;
        case GLFW_KEY_END:       return Key_End;
        default:
            break;
    }
    if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12) {
        return Key_F1 + (glfwKey - GLFW_KEY_F1);
    }
    if (glfwKey >= 32 && glfwKey <= 96) {
        return glfwKey;  // GLFW already reports uppercase ASCII.
    }
    return Key_Unknown;
}

// ---------------------------------------------------------------------------

class GlfwSurface final : public Surface {
public:
    ~GlfwSurface() override {
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (initialised_) {
            ReleaseGlfw();
        }
    }

    CgResult Create(const SurfaceDesc& desc) {
        if (!AcquireGlfw()) {
            return CgResult::RenderError;
        }
        initialised_ = true;

        if (!glfwVulkanSupported()) {
            return core::SetError(CgResult::NotSupported,
                                  "GLFW cannot find a Vulkan loader on this machine");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No GL context; this is Vulkan.
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        const int width = static_cast<int>(std::max<uint32_t>(desc.width, 1));
        const int height = static_cast<int>(std::max<uint32_t>(desc.height, 1));
        window_ = glfwCreateWindow(width, height, desc.title ? desc.title : "CadGeom", nullptr,
                                   nullptr);
        if (!window_) {
            const char* description = nullptr;
            glfwGetError(&description);
            return core::SetError(CgResult::RenderError, "glfwCreateWindow failed: %s",
                                  description ? description : "no detail available");
        }

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, &OnFramebufferSize);
        glfwSetCursorPosCallback(window_, &OnCursorPos);
        glfwSetMouseButtonCallback(window_, &OnMouseButton);
        glfwSetScrollCallback(window_, &OnScroll);
        glfwSetKeyCallback(window_, &OnKey);
        return CgResult::Ok;
    }

    CgResult CreateVkSurface(VkInstance instance, VkSurfaceKHR& out) override {
        out = VK_NULL_HANDLE;
        CG_VK_REQUIRE(glfwCreateWindowSurface(instance, window_, nullptr, &out),
                      "glfwCreateWindowSurface");
        return CgResult::Ok;
    }

    void GetExtent(uint32_t& width, uint32_t& height) const override {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        width = static_cast<uint32_t>(std::max(w, 0));
        height = static_cast<uint32_t>(std::max(h, 0));
    }

    bool IsMinimized() const override {
        uint32_t w = 0;
        uint32_t h = 0;
        GetExtent(w, h);
        return w == 0 || h == 0;
    }

    bool ShouldClose() const override { return glfwWindowShouldClose(window_) != 0; }

private:
    static GlfwSurface* From(GLFWwindow* window) {
        return static_cast<GlfwSurface*>(glfwGetWindowUserPointer(window));
    }

    /// Window coordinates are not framebuffer pixels on a scaled display, and
    /// everything downstream — picking tolerances, pan distances — is in
    /// framebuffer pixels because that is what the viewport is measured in.
    void CursorInPixels(double& x, double& y) const {
        int windowW = 0;
        int windowH = 0;
        int fbW = 0;
        int fbH = 0;
        glfwGetWindowSize(window_, &windowW, &windowH);
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (windowW > 0 && windowH > 0) {
            x *= static_cast<double>(fbW) / static_cast<double>(windowW);
            y *= static_cast<double>(fbH) / static_cast<double>(windowH);
        }
    }

    void Deliver(const MouseEvent& e) {
        if (callbacks_.onMouse) {
            callbacks_.onMouse(e);
        }
    }

    static void OnFramebufferSize(GLFWwindow* window, int width, int height) {
        GlfwSurface* self = From(window);
        if (self && self->callbacks_.onResize) {
            self->callbacks_.onResize(static_cast<uint32_t>(std::max(width, 0)),
                                      static_cast<uint32_t>(std::max(height, 0)));
        }
    }

    static void OnCursorPos(GLFWwindow* window, double x, double y) {
        GlfwSurface* self = From(window);
        if (!self) {
            return;
        }
        self->CursorInPixels(x, y);
        self->cursorX_ = x;
        self->cursorY_ = y;

        MouseEvent e{};
        e.button = self->heldButton_;
        e.action = MouseAction::Move;
        e.x = x;
        e.y = y;
        e.mods = self->mods_;
        self->Deliver(e);
    }

    static void OnMouseButton(GLFWwindow* window, int button, int action, int mods) {
        GlfwSurface* self = From(window);
        if (!self) {
            return;
        }
        const MouseButton translated = TranslateButton(button);
        self->mods_ = TranslateMods(mods);

        MouseEvent e{};
        e.button = translated;
        e.x = self->cursorX_;
        e.y = self->cursorY_;
        e.mods = self->mods_;

        if (action == GLFW_PRESS) {
            self->heldButton_ = translated;
            e.action = self->IsDoubleClick(translated) ? MouseAction::DoubleClick
                                                       : MouseAction::Down;
        } else {
            self->heldButton_ = MouseButton::None;
            e.action = MouseAction::Up;
        }
        self->Deliver(e);
    }

    static void OnScroll(GLFWwindow* window, double /*dx*/, double dy) {
        GlfwSurface* self = From(window);
        if (!self) {
            return;
        }
        MouseEvent e{};
        e.button = MouseButton::None;
        e.action = MouseAction::Wheel;
        e.x = self->cursorX_;
        e.y = self->cursorY_;
        e.wheelDelta = dy;
        e.mods = self->mods_;
        self->Deliver(e);
    }

    static void OnKey(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
        GlfwSurface* self = From(window);
        if (!self || !self->callbacks_.onKey) {
            return;
        }
        self->mods_ = TranslateMods(mods);

        KeyEvent e{};
        e.key = TranslateKey(key);
        e.action = action == GLFW_RELEASE  ? KeyAction::Up
                   : action == GLFW_REPEAT ? KeyAction::Repeat
                                           : KeyAction::Down;
        e.mods = self->mods_;
        self->callbacks_.onKey(e);
    }

    /// GLFW has no double-click notion, so it is synthesised here rather than
    /// in every tool that wants one.
    bool IsDoubleClick(MouseButton button) {
        const double now = glfwGetTime();
        // Not called `near`: the Windows headers still define that as a macro.
        const bool nearby = std::fabs(cursorX_ - lastClickX_) < 4.0 &&
                            std::fabs(cursorY_ - lastClickY_) < 4.0;
        const bool quick = (now - lastClickTime_) < 0.35;
        const bool same = button == lastClickButton_;

        lastClickTime_ = now;
        lastClickX_ = cursorX_;
        lastClickY_ = cursorY_;
        lastClickButton_ = button;

        if (same && nearby && quick) {
            // Consume it, so a triple click is not two double clicks.
            lastClickTime_ = 0.0;
            return true;
        }
        return false;
    }

    GLFWwindow* window_{nullptr};
    bool initialised_{false};

    double cursorX_{0.0};
    double cursorY_{0.0};
    uint32_t mods_{KeyMod_None};
    MouseButton heldButton_{MouseButton::None};

    double lastClickTime_{0.0};
    double lastClickX_{0.0};
    double lastClickY_{0.0};
    MouseButton lastClickButton_{MouseButton::None};
};

// ---------------------------------------------------------------------------

class NativeSurface final : public Surface {
public:
    CgResult Create(const SurfaceDesc& desc) {
        desc_ = desc;
#if defined(_WIN32)
        if (desc.kind == SurfaceKind::NativeWin32) {
            if (!desc.nativeWindow) {
                return core::SetError(CgResult::InvalidArgument,
                                      "SurfaceKind::NativeWin32 needs nativeWindow set to an HWND");
            }
            if (IsWindow(static_cast<HWND>(desc.nativeWindow)) == FALSE) {
                return core::SetError(CgResult::InvalidArgument,
                                      "SurfaceDesc::nativeWindow is not a live HWND");
            }
            return CgResult::Ok;
        }
#endif
        return core::SetError(CgResult::NotSupported,
                              "this build has no backend for that native surface kind; "
                              "Win32 is implemented, X11/Wayland/Cocoa are not");
    }

    CgResult CreateVkSurface(VkInstance instance, VkSurfaceKHR& out) override {
        out = VK_NULL_HANDLE;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        VkWin32SurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        info.hinstance = desc_.nativeDisplay
                             ? static_cast<HINSTANCE>(desc_.nativeDisplay)
                             : reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(
                                   static_cast<HWND>(desc_.nativeWindow), GWLP_HINSTANCE));
        info.hwnd = static_cast<HWND>(desc_.nativeWindow);
        CG_VK_REQUIRE(vkCreateWin32SurfaceKHR(instance, &info, nullptr, &out),
                      "vkCreateWin32SurfaceKHR");
        return CgResult::Ok;
#else
        (void)instance;
        return core::SetError(CgResult::NotSupported, "no native surface backend in this build");
#endif
    }

    void SetExtent(uint32_t width, uint32_t height) override {
        desc_.width = width;
        desc_.height = height;
    }

    void GetExtent(uint32_t& width, uint32_t& height) const override {
        width = desc_.width;
        height = desc_.height;
#if defined(_WIN32)
        RECT rect{};
        if (desc_.nativeWindow &&
            GetClientRect(static_cast<HWND>(desc_.nativeWindow), &rect) != FALSE) {
            width = static_cast<uint32_t>(std::max<LONG>(rect.right - rect.left, 0));
            height = static_cast<uint32_t>(std::max<LONG>(rect.bottom - rect.top, 0));
        }
#endif
    }

    bool IsMinimized() const override {
        uint32_t w = 0;
        uint32_t h = 0;
        GetExtent(w, h);
        return w == 0 || h == 0;
    }

private:
    SurfaceDesc desc_{};
};

// ---------------------------------------------------------------------------

class HeadlessSurface final : public Surface {
public:
    explicit HeadlessSurface(const SurfaceDesc& desc)
        : width_(std::max<uint32_t>(desc.width, 1)), height_(std::max<uint32_t>(desc.height, 1)) {}

    CgResult CreateVkSurface(VkInstance, VkSurfaceKHR& out) override {
        out = VK_NULL_HANDLE;
        return CgResult::Ok;
    }

    void GetExtent(uint32_t& width, uint32_t& height) const override {
        width = width_;
        height = height_;
    }

    void SetExtent(uint32_t width, uint32_t height) override {
        width_ = std::max<uint32_t>(width, 1);
        height_ = std::max<uint32_t>(height, 1);
    }

    bool IsMinimized() const override { return false; }
    bool PresentsToScreen() const override { return false; }

private:
    uint32_t width_;
    uint32_t height_;
};

} // namespace

std::unique_ptr<Surface> CreateSurface(const SurfaceDesc& desc) {
    switch (desc.kind) {
        case SurfaceKind::Glfw: {
            auto surface = std::make_unique<GlfwSurface>();
            if (CgFailed(surface->Create(desc))) {
                return nullptr;
            }
            return surface;
        }
        case SurfaceKind::Headless:
            return std::make_unique<HeadlessSurface>(desc);
        case SurfaceKind::NativeWin32:
        case SurfaceKind::NativeXlib:
        case SurfaceKind::NativeWayland:
        case SurfaceKind::NativeCocoa: {
            auto surface = std::make_unique<NativeSurface>();
            if (CgFailed(surface->Create(desc))) {
                return nullptr;
            }
            return surface;
        }
    }
    core::SetError(CgResult::InvalidArgument, "unknown SurfaceKind %d",
                   static_cast<int>(desc.kind));
    return nullptr;
}

void PumpWindowEvents() {
    if (g_glfwUsers > 0) {
        glfwPollEvents();
    }
}

} // namespace cadgeom::render
