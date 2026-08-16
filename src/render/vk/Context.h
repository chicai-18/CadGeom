// Instance, device, queue and allocator — initialised once and shared by every
// viewport (docs/architecture.md §4.1).
//
// Creation is deliberately lazy: a host that never asks for a viewport never
// touches a GPU, which is what keeps the geometry kernel and the tests
// Vulkan-free.
#pragma once

#include "render/vk/Common.h"

#include <functional>
#include <string>

// VMA handles, declared here so this header does not drag the 20k-line
// vk_mem_alloc.h into every translation unit that needs a device pointer.
// These are the same typedefs VMA itself emits.
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace cadgeom::render::vk {

class Context {
public:
    struct Config {
        const char* applicationName{nullptr};
        bool enableValidation{false};
        /// False for a headless-only engine: skips the surface extensions and
        /// the swapchain device extension.
        bool needPresentation{true};
    };

    Context() = default;
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    CgResult Initialize(const Config& config);
    void Shutdown();

    bool IsValid() const { return device_ != VK_NULL_HANDLE; }

    VkInstance Instance() const { return instance_; }
    VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    VkDevice Device() const { return device_; }
    VkQueue GraphicsQueue() const { return graphicsQueue_; }
    uint32_t GraphicsFamily() const { return graphicsFamily_; }
    VmaAllocator Allocator() const { return allocator_; }

    /// UTF-8, never null — a placeholder before Initialize().
    const char* DeviceName() const { return deviceName_.c_str(); }

    /// True when the device can rasterise VK_POLYGON_MODE_LINE, which is what
    /// RenderMode::Wireframe rides on until M4 brings the real EdgePass.
    bool SupportsFillModeNonSolid() const { return fillModeNonSolid_; }

    /// Records and runs a one-shot command buffer, waiting for it to finish.
    /// Uploads and screenshot readbacks use it; nothing on the frame path does.
    CgResult ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    CgResult WaitIdle();

private:
    CgResult CreateInstance(const Config& config);
    CgResult CreateDebugMessenger();
    CgResult SelectPhysicalDevice(const Config& config);
    CgResult CreateDevice(const Config& config);
    CgResult CreateAllocator();
    CgResult CreateImmediateContext();

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    uint32_t graphicsFamily_{0};
    VmaAllocator allocator_{nullptr};

    VkCommandPool immediatePool_{VK_NULL_HANDLE};
    VkCommandBuffer immediateCmd_{VK_NULL_HANDLE};
    VkFence immediateFence_{VK_NULL_HANDLE};

    std::string deviceName_{"<no device>"};
    bool fillModeNonSolid_{false};
    bool validationEnabled_{false};
};

} // namespace cadgeom::render::vk
