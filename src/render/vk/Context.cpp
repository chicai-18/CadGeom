#include "render/vk/Context.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <vector>

namespace cadgeom::render::vk {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool HasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& l) {
        return std::string(l.layerName) == name;
    });
}

bool HasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(), [name](const VkExtensionProperties& e) {
        return std::string(e.extensionName) == name;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*userData*/) {
    if (!data || !data->pMessage) {
        return VK_FALSE;
    }
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        CG_ERROR("[vulkan] %s", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        CG_WARN("[vulkan] %s", data->pMessage);
    } else {
        CG_DEBUG("[vulkan] %s", data->pMessage);
    }
    // Always VK_FALSE: the callback reports, it never aborts the offending call.
    return VK_FALSE;
}

void FillDebugMessengerInfo(VkDebugUtilsMessengerCreateInfoEXT& info) {
    info = VkDebugUtilsMessengerCreateInfoEXT{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = &DebugCallback;
}

const char* DeviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "other";
    }
}

} // namespace

Context::~Context() {
    Shutdown();
}

CgResult Context::Initialize(const Config& config) {
    if (IsValid()) {
        return CgResult::Ok;
    }

    validationEnabled_ = config.enableValidation;

    CgResult r = CreateInstance(config);
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    if (validationEnabled_) {
        r = CreateDebugMessenger();
        if (CgFailed(r)) {
            Shutdown();
            return r;
        }
    }
    r = SelectPhysicalDevice(config);
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    r = CreateDevice(config);
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    r = CreateAllocator();
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    r = CreateImmediateContext();
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }

    CG_INFO("vulkan device ready: %s", deviceName_.c_str());
    return CgResult::Ok;
}

CgResult Context::CreateInstance(const Config& config) {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS) {
        loaderVersion = VK_API_VERSION_1_0;
    }
    if (loaderVersion < VK_API_VERSION_1_3) {
        return core::SetError(CgResult::NotSupported,
                              "the Vulkan loader on this machine reports %u.%u; CadGeom needs "
                              "1.3 (dynamic rendering and synchronization2). Update the GPU "
                              "driver or install a current runtime.",
                              VK_API_VERSION_MAJOR(loaderVersion),
                              VK_API_VERSION_MINOR(loaderVersion));
    }

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    if (layerCount > 0) {
        vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    }

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extProps(extCount);
    if (extCount > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &extCount, extProps.data());
    }

    std::vector<const char*> layers;
    if (validationEnabled_) {
        if (HasLayer(layerProps, kValidationLayer)) {
            layers.push_back(kValidationLayer);
        } else {
            // Not fatal: validation is a development aid, and a machine with
            // only the runtime installed still has to be able to render.
            CG_WARN("validation requested but %s is not installed; continuing without it",
                    kValidationLayer);
            validationEnabled_ = false;
        }
    }

    std::vector<const char*> extensions;
    if (config.needPresentation) {
        if (!HasExtension(extProps, VK_KHR_SURFACE_EXTENSION_NAME)) {
            return core::SetError(CgResult::NotSupported,
                                  "the Vulkan loader does not expose " VK_KHR_SURFACE_EXTENSION_NAME
                                  "; only headless viewports are possible on this machine");
        }
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        if (!HasExtension(extProps, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
            return core::SetError(CgResult::NotSupported,
                                  "the Vulkan loader does not expose "
                                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        }
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    }
    if (validationEnabled_ && HasExtension(extProps, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = config.applicationName ? config.applicationName : "CadGeom host";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "CadGeom";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    // Chained so the layers can report on instance creation and destruction
    // themselves — the window in which the standalone messenger does not exist.
    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    if (validationEnabled_) {
        FillDebugMessengerInfo(debugInfo);
        info.pNext = &debugInfo;
    }

    CG_VK_REQUIRE(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
    return CgResult::Ok;
}

CgResult Context::CreateDebugMessenger() {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create) {
        CG_WARN("VK_EXT_debug_utils is unavailable; validation messages will not be forwarded");
        return CgResult::Ok;
    }

    VkDebugUtilsMessengerCreateInfoEXT info{};
    FillDebugMessengerInfo(info);
    CG_VK_REQUIRE(create(instance_, &info, nullptr, &debugMessenger_),
                  "vkCreateDebugUtilsMessengerEXT");
    return CgResult::Ok;
}

CgResult Context::SelectPhysicalDevice(const Config& config) {
    uint32_t count = 0;
    CG_VK_REQUIRE(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                  "vkEnumeratePhysicalDevices");
    if (count == 0) {
        return core::SetError(CgResult::NotSupported,
                              "no Vulkan-capable GPU found on this machine");
    }
    std::vector<VkPhysicalDevice> devices(count);
    CG_VK_REQUIRE(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                  "vkEnumeratePhysicalDevices");

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestFamily = 0;
    int bestScore = -1;
    std::string rejections;

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);

        const auto reject = [&](const char* why) {
            rejections += "\n  - ";
            rejections += props.deviceName;
            rejections += ": ";
            rejections += why;
        };

        if (props.apiVersion < VK_API_VERSION_1_3) {
            reject("driver predates Vulkan 1.3");
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features2);

        // shaderDemoteToHelperInvocation is in the list because glslc compiles
        // `discard` to OpDemoteToHelperInvocation when targeting Vulkan 1.3,
        // and the grid shader discards. It is core 1.3 but still opt-in.
        if (!features13.dynamicRendering || !features13.synchronization2 ||
            !features13.shaderDemoteToHelperInvocation) {
            reject("no dynamic rendering / synchronization2 / demote-to-helper");
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        uint32_t family = UINT32_MAX;
        for (uint32_t i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
            // Checked without a surface on purpose: the device is created once,
            // before any viewport exists, and a graphics family that cannot
            // present would only fail much later at swapchain creation.
            if (config.needPresentation &&
                vkGetPhysicalDeviceWin32PresentationSupportKHR(device, i) != VK_TRUE) {
                continue;
            }
#else
            (void)config;
#endif
            family = i;
            break;
        }
        if (family == UINT32_MAX) {
            reject("no graphics queue family that can present");
            continue;
        }

        int score = 0;
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 400; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 300; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 200; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 100; break;
            default:                                     score = 50;  break;
        }
        CG_DEBUG("candidate GPU: %s (%s, driver api %u.%u)", props.deviceName,
                 DeviceTypeName(props.deviceType), VK_API_VERSION_MAJOR(props.apiVersion),
                 VK_API_VERSION_MINOR(props.apiVersion));

        if (score > bestScore) {
            bestScore = score;
            best = device;
            bestFamily = family;
        }
    }

    if (best == VK_NULL_HANDLE) {
        return core::SetError(CgResult::NotSupported,
                              "no GPU meets CadGeom's requirements (Vulkan 1.3 with dynamic "
                              "rendering and synchronization2):%s",
                              rejections.c_str());
    }

    physicalDevice_ = best;
    graphicsFamily_ = bestFamily;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    deviceName_ = props.deviceName;

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &features);
    fillModeNonSolid_ = features.fillModeNonSolid == VK_TRUE;

    return CgResult::Ok;
}

CgResult Context::CreateDevice(const Config& config) {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extProps(extCount);
    if (extCount > 0) {
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, extProps.data());
    }

    std::vector<const char*> extensions;
    if (config.needPresentation) {
        if (!HasExtension(extProps, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            return core::SetError(CgResult::NotSupported,
                                  "%s cannot present: no " VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                  deviceName_.c_str());
        }
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;
    features2.features.fillModeNonSolid = fillModeNonSolid_ ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &features2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    CG_VK_REQUIRE(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    return CgResult::Ok;
}

CgResult Context::CreateAllocator() {
    VmaAllocatorCreateInfo info{};
    info.physicalDevice = physicalDevice_;
    info.device = device_;
    info.instance = instance_;
    info.vulkanApiVersion = VK_API_VERSION_1_3;

    CG_VK_REQUIRE(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
    return CgResult::Ok;
}

CgResult Context::CreateImmediateContext() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    CG_VK_REQUIRE(vkCreateCommandPool(device_, &poolInfo, nullptr, &immediatePool_),
                  "vkCreateCommandPool (immediate)");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = immediatePool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    CG_VK_REQUIRE(vkAllocateCommandBuffers(device_, &allocInfo, &immediateCmd_),
                  "vkAllocateCommandBuffers (immediate)");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    CG_VK_REQUIRE(vkCreateFence(device_, &fenceInfo, nullptr, &immediateFence_),
                  "vkCreateFence (immediate)");
    return CgResult::Ok;
}

CgResult Context::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
    if (!IsValid()) {
        return core::SetError(CgResult::InvalidState, "ImmediateSubmit before device creation");
    }

    CG_VK_REQUIRE(vkResetFences(device_, 1, &immediateFence_), "vkResetFences (immediate)");
    CG_VK_REQUIRE(vkResetCommandBuffer(immediateCmd_, 0), "vkResetCommandBuffer (immediate)");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CG_VK_REQUIRE(vkBeginCommandBuffer(immediateCmd_, &begin), "vkBeginCommandBuffer (immediate)");
    record(immediateCmd_);
    CG_VK_REQUIRE(vkEndCommandBuffer(immediateCmd_), "vkEndCommandBuffer (immediate)");

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = immediateCmd_;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    CG_VK_REQUIRE(vkQueueSubmit2(graphicsQueue_, 1, &submit, immediateFence_),
                  "vkQueueSubmit2 (immediate)");
    CG_VK_REQUIRE(vkWaitForFences(device_, 1, &immediateFence_, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences (immediate)");
    return CgResult::Ok;
}

CgResult Context::WaitIdle() {
    if (!IsValid()) {
        return CgResult::Ok;
    }
    CG_VK_REQUIRE(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    return CgResult::Ok;
}

void Context::Shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);

        if (immediateFence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, immediateFence_, nullptr);
            immediateFence_ = VK_NULL_HANDLE;
        }
        if (immediatePool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, immediatePool_, nullptr);
            immediatePool_ = VK_NULL_HANDLE;
            immediateCmd_ = VK_NULL_HANDLE;
        }
        if (allocator_ != nullptr) {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) {
            destroy(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
    deviceName_ = "<no device>";
}

} // namespace cadgeom::render::vk
