#include "renderer.h"
#include "svg_parser.h"
#include <shaderc/shaderc.hpp>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cassert>
#include <chrono>

#define VK_CHECK(expr, msg) \
    do { VkResult _r = (expr); \
        if(_r != VK_SUCCESS) { \
            char _buf[256]; \
            snprintf(_buf, sizeof(_buf), "Vulkan error: %s (VkResult=%d)", msg, (int)_r); \
            throw std::runtime_error(_buf); \
        } \
    } while(0)

#define SAFE_VK(p, fn) do{ if(p != VK_NULL_HANDLE){ fn; p = VK_NULL_HANDLE; } }while(0)

#ifdef _WIN32
    #ifdef _DEBUG
        #define RLOG(fmt, ...) do { \
            char _rlbuf[512]; \
            snprintf(_rlbuf, sizeof(_rlbuf), "[RNDR] " fmt "\n", ##__VA_ARGS__); \
            OutputDebugStringA(_rlbuf); \
            printf("%s", _rlbuf); \
        } while(0)
    #else
        #define RLOG(fmt, ...) do {} while(0)
    #endif
#else
    #ifdef _DEBUG
        #define RLOG(fmt, ...) do { \
            printf("[RNDR] " fmt "\n", ##__VA_ARGS__); \
        } while(0)
    #else
        #define RLOG(fmt, ...) do {} while(0)
    #endif
#endif

static const char* VS_GLSL = R"GLSL(
#version 450
layout(set=0, binding=0) uniform UBO { mat4 proj; } u;
layout(location=0) in  vec2 inPos;
layout(location=1) in  vec4 inColor;
layout(location=0) out vec4 fragColor;
void main() {
    gl_Position = u.proj * vec4(inPos, 0.0, 1.0);
    fragColor   = inColor;
}
)GLSL";

static const char* FS_GLSL = R"GLSL(
#version 450
layout(location=0) in  vec4 fragColor;
layout(location=0) out vec4 outColor;
void main() { outColor = fragColor; }
)GLSL";

struct Mat4 { float m[4][4] = {}; };
static Mat4 ortho2D(float l, float r, float b, float t) {
    Mat4 M;
    M.m[0][0] =  2.f / (r - l);
    M.m[1][1] =  2.f / (t - b);
    M.m[2][2] =  1.f;
    M.m[3][0] = -(r + l) / (r - l);
    M.m[3][1] = -(t + b) / (t - b);
    M.m[3][3] =  1.f;
    return M;
}

VulkanSVGRenderer::~VulkanSVGRenderer() {
    if(m_device) vkDeviceWaitIdle(m_device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_queryPools[i] != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_device, m_queryPools[i], nullptr);
            m_queryPools[i] = VK_NULL_HANDLE;
        }
    }

    destroyGeometryBuffers();

    for(uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        SAFE_VK(m_imageAvailSem[i], vkDestroySemaphore(m_device, m_imageAvailSem[i], nullptr));
        SAFE_VK(m_frameFence[i],    vkDestroyFence    (m_device, m_frameFence[i],    nullptr));
    }

    cleanupSwapchain();

    SAFE_VK(m_descSetLayout, vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr));
    SAFE_VK(m_cmdPool,       vkDestroyCommandPool(m_device, m_cmdPool, nullptr));
    SAFE_VK(m_device,        vkDestroyDevice(m_device, nullptr));
    SAFE_VK(m_surface,       vkDestroySurfaceKHR(m_instance, m_surface, nullptr));

#ifdef _DEBUG
    if(m_debugMessenger != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if(fn) fn(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    SAFE_VK(m_instance, vkDestroyInstance(m_instance, nullptr));
}

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "[VK] %s\n", pData->pMessage);
    #ifdef _WIN32
        OutputDebugStringA(buf);
    #endif
    printf("%s", buf);
    return VK_FALSE;
}
#endif

#ifdef _WIN32
bool VulkanSVGRenderer::createSurface(HWND hwnd) {
    VkWin32SurfaceCreateInfoKHR ci = {};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hwnd      = hwnd;
    ci.hinstance = GetModuleHandleW(nullptr);
    VK_CHECK(vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &m_surface),
             "vkCreateWin32SurfaceKHR");
    return true;
}

bool VulkanSVGRenderer::createInstance() {
    VkApplicationInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "SVG Renderer";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion         = VK_API_VERSION_1_1;

#ifdef _WIN32
    const char* surfaceExt = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    const char* surfaceExt = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surfaceExt,
#ifdef _DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    constexpr uint32_t layerCount = 1;
#else
    const char** layers = nullptr;
    constexpr uint32_t layerCount = 0;
#endif

    VkInstanceCreateInfo ci = {};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount       = layerCount;
    ci.ppEnabledLayerNames     = layers;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dci = {};
    dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if(fn) fn(m_instance, &dci, nullptr, &m_debugMessenger);
#endif

    return true;
}

bool VulkanSVGRenderer::init(HWND hwnd, int width, int height) {
    m_width  = width;
    m_height = height;
    RLOG("init: %dx%d", width, height);

    if(!createInstance())              { RLOG("FAILED createInstance");              return false; }
    if(!createSurface(hwnd))           { RLOG("FAILED createSurface");               return false; }
    if(!pickPhysicalDevice())          { RLOG("FAILED pickPhysicalDevice");           return false; }
    if(!createLogicalDevice())         { RLOG("FAILED createLogicalDevice");          return false; }
    if(!createSwapchain())             { RLOG("FAILED createSwapchain");              return false; }
    if(!createDepthStencilResources()) { RLOG("FAILED createDepthStencilResources"); return false; }
    if(!createMSAAResources())         { RLOG("FAILED createMSAAResources");          return false; }
    if(!createRenderPass())            { RLOG("FAILED createRenderPass");             return false; }
    if(!createDescriptorSetLayout())   { RLOG("FAILED createDescriptorSetLayout");   return false; }
    if(!createPipeline())              { RLOG("FAILED createPipeline");               return false; }
    if(!createFramebuffers())          { RLOG("FAILED createFramebuffers");           return false; }
    if(!createCommandPool())           { RLOG("FAILED createCommandPool");            return false; }
    if(!createUniformBuffers())        { RLOG("FAILED createUniformBuffers");         return false; }
    if(!createDescriptorPool())        { RLOG("FAILED createDescriptorPool");         return false; }
    if(!createDescriptorSets())        { RLOG("FAILED createDescriptorSets");         return false; }
    if(!createCommandBuffers())        { RLOG("FAILED createCommandBuffers");         return false; }
    if(!createSyncObjects())           { RLOG("FAILED createSyncObjects");            return false; }
    RLOG("init complete");
    return true;
}
#else
bool VulkanSVGRenderer::createSurface(Display* display, Window window) {
    VkXlibSurfaceCreateInfoKHR ci = {};
    ci.sType   = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    ci.dpy     = display;
    ci.window  = window;
    VK_CHECK(vkCreateXlibSurfaceKHR(m_instance, &ci, nullptr, &m_surface),
             "vkCreateXlibSurfaceKHR");
    return true;
}

bool VulkanSVGRenderer::createInstance() {
    VkApplicationInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "SVG Renderer";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion         = VK_API_VERSION_1_1;

    // Select platform-specific surface extension
#ifdef _WIN32
    const char* surfaceExt = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    const char* surfaceExt = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surfaceExt,
#ifdef _DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    constexpr uint32_t layerCount = 1;
#else
    const char** layers = nullptr;
    constexpr uint32_t layerCount = 0;
#endif

    VkInstanceCreateInfo ci = {};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount       = layerCount;
    ci.ppEnabledLayerNames     = layers;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dci = {};
    dci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if(fn) fn(m_instance, &dci, nullptr, &m_debugMessenger);
#endif

    return true;
}

bool VulkanSVGRenderer::init(Display* display, Window window, int width, int height) {
    m_width  = width;
    m_height = height;
    RLOG("init (X11): %dx%d", width, height);

    if(!createInstance())              { RLOG("FAILED createInstance");              return false; }
    if(!createSurface(display, window)) { RLOG("FAILED createSurface");               return false; }
    if(!pickPhysicalDevice())          { RLOG("FAILED pickPhysicalDevice");           return false; }
    if(!createLogicalDevice())         { RLOG("FAILED createLogicalDevice");          return false; }
    if(!createSwapchain())             { RLOG("FAILED createSwapchain");              return false; }
    if(!createDepthStencilResources()) { RLOG("FAILED createDepthStencilResources"); return false; }
    if(!createMSAAResources())         { RLOG("FAILED createMSAAResources");          return false; }
    if(!createRenderPass())            { RLOG("FAILED createRenderPass");             return false; }
    if(!createDescriptorSetLayout())   { RLOG("FAILED createDescriptorSetLayout");   return false; }
    if(!createPipeline())              { RLOG("FAILED createPipeline");               return false; }
    if(!createFramebuffers())          { RLOG("FAILED createFramebuffers");           return false; }
    if(!createCommandPool())           { RLOG("FAILED createCommandPool");            return false; }
    if(!createUniformBuffers())        { RLOG("FAILED createUniformBuffers");         return false; }
    if(!createDescriptorPool())        { RLOG("FAILED createDescriptorPool");         return false; }
    if(!createDescriptorSets())        { RLOG("FAILED createDescriptorSets");         return false; }
    if(!createCommandBuffers())        { RLOG("FAILED createCommandBuffers");         return false; }
    if(!createSyncObjects())           { RLOG("FAILED createSyncObjects");            return false; }
    RLOG("init complete");
    return true;
}
#endif

bool VulkanSVGRenderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if(count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

    for(auto dev : devs) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());

        uint32_t gfx = UINT32_MAX, pres = UINT32_MAX;
        for(uint32_t i = 0; i < qcount; i++) {
            if(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfx = i;
            VkBool32 ps = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &ps);
            if(ps) pres = i;
        }
        if(gfx == UINT32_MAX || pres == UINT32_MAX) continue;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for(auto& e : exts)
            if(strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                { hasSwapchain = true; break; }
        if(!hasSwapchain) continue;

        uint32_t fmtCount = 0, modeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surface, &fmtCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surface, &modeCount, nullptr);
        if(fmtCount == 0 || modeCount == 0) continue;

        m_physDevice     = dev;
        m_graphicsFamily = gfx;
        m_presentFamily  = pres;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        RLOG("GPU candidate: '%s'  type=%d  gfxQ=%u presQ=%u",
             props.deviceName, (int)props.deviceType, gfx, pres);
        if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
    }

    if(m_physDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable Vulkan device found");
    m_msaaSamples = getMaxSampleCount();
    RLOG("Selected GPU: graphicsFamily=%u presentFamily=%u msaaSamples=%d",
         m_graphicsFamily, m_presentFamily, (int)m_msaaSamples);
    return true;
}

bool VulkanSVGRenderer::createLogicalDevice() {
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;

    auto addQueue = [&](uint32_t family) {
        VkDeviceQueueCreateInfo qi = {};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &prio;
        qcis.push_back(qi);
    };
    addQueue(m_graphicsFamily);
    if(m_presentFamily != m_graphicsFamily) addQueue(m_presentFamily);

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo ci = {};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = (uint32_t)qcis.size();
    ci.pQueueCreateInfos       = qcis.data();
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = devExts;
    ci.pEnabledFeatures        = &features;
    VK_CHECK(vkCreateDevice(m_physDevice, &ci, nullptr, &m_device), "vkCreateDevice");

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily,  0, &m_presentQueue);
    return true;
}

bool VulkanSVGRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount, fmts.data());

    VkSurfaceFormatKHR chosenFmt = fmts[0];
    for(auto& f : fmts)
        if(f.format == VK_FORMAT_B8G8R8A8_UNORM &&
           f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            { chosenFmt = f; break; }
    m_swapFormat = chosenFmt.format;

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface, &modeCount, modes.data());

    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if(caps.currentExtent.width != UINT32_MAX) {
        m_swapExtent = caps.currentExtent;
    } else {
        m_swapExtent.width  = std::max(caps.minImageExtent.width,
                              std::min(caps.maxImageExtent.width,  (uint32_t)m_width));
        m_swapExtent.height = std::max(caps.minImageExtent.height,
                              std::min(caps.maxImageExtent.height, (uint32_t)m_height));
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if(caps.maxImageCount > 0) imgCount = std::min(imgCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = m_surface;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = chosenFmt.format;
    ci.imageColorSpace  = chosenFmt.colorSpace;
    ci.imageExtent      = m_swapExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = m_presentMode;
    ci.clipped          = VK_TRUE;

    if(m_graphicsFamily != m_presentFamily) {
        uint32_t families[] = { m_graphicsFamily, m_presentFamily };
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain),
             "vkCreateSwapchainKHR");

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_swapImages.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapImages.data());
    RLOG("Swapchain: %ux%u  format=%d  images=%u  presentMode=%d",
         m_swapExtent.width, m_swapExtent.height,
         (int)m_swapFormat, actualCount, (int)m_presentMode);

    m_swapImageViews.resize(actualCount);
    for(uint32_t i = 0; i < actualCount; i++) {
        VkImageViewCreateInfo ivci = {};
        ivci.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                       = m_swapImages[i];
        ivci.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                      = m_swapFormat;
        ivci.components                  = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                             VK_COMPONENT_SWIZZLE_IDENTITY,
                                             VK_COMPONENT_SWIZZLE_IDENTITY,
                                             VK_COMPONENT_SWIZZLE_IDENTITY };
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_swapImageViews[i]),
                 "vkCreateImageView");
    }
    return true;
}

VkFormat VulkanSVGRenderer::findDepthStencilFormat() {
    VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    for(auto fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physDevice, fmt, &props);
        if(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    throw std::runtime_error("No suitable depth/stencil format found");
}

bool VulkanSVGRenderer::createDepthStencilResources() {
    m_dsFormat = findDepthStencilFormat();
    RLOG("DepthStencil: format=%d  extent=%ux%u  samples=%d",
         (int)m_dsFormat, m_swapExtent.width, m_swapExtent.height, (int)m_msaaSamples);

    VkImageCreateInfo ici = {};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = m_dsFormat;
    ici.extent        = { m_swapExtent.width, m_swapExtent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = m_msaaSamples;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(m_device, &ici, nullptr, &m_dsImage), "DS vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_dsImage, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &m_dsMem), "DS vkAllocateMemory");
    vkBindImageMemory(m_device, m_dsImage, m_dsMem, 0);

    VkImageViewCreateInfo ivci = {};
    ivci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image      = m_dsImage;
    ivci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format     = m_dsFormat;
    ivci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_dsView), "DS vkCreateImageView");
    return true;
}

void VulkanSVGRenderer::cleanupDepthStencilResources() {
    if(!m_device) return;
    SAFE_VK(m_dsView,  vkDestroyImageView(m_device, m_dsView,  nullptr));
    SAFE_VK(m_dsImage, vkDestroyImage    (m_device, m_dsImage, nullptr));
    SAFE_VK(m_dsMem,   vkFreeMemory      (m_device, m_dsMem,   nullptr));
}

VkSampleCountFlagBits VulkanSVGRenderer::getMaxSampleCount() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    VkSampleCountFlags c = props.limits.framebufferColorSampleCounts &
                           props.limits.framebufferDepthSampleCounts;
    if(c & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if(c & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if(c & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

bool VulkanSVGRenderer::createMSAAResources() {
    if(m_msaaSamples == VK_SAMPLE_COUNT_1_BIT) return true;

    VkImageCreateInfo ici = {};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = m_swapFormat;
    ici.extent        = { m_swapExtent.width, m_swapExtent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = m_msaaSamples;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(m_device, &ici, nullptr, &m_msaaImage), "MSAA vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_msaaImage, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &m_msaaMem), "MSAA vkAllocateMemory");
    vkBindImageMemory(m_device, m_msaaImage, m_msaaMem, 0);

    VkImageViewCreateInfo ivci = {};
    ivci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image      = m_msaaImage;
    ivci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format     = m_swapFormat;
    ivci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_msaaView), "MSAA vkCreateImageView");
    return true;
}

void VulkanSVGRenderer::cleanupMSAAResources() {
    if(!m_device) return;
    SAFE_VK(m_msaaView,  vkDestroyImageView(m_device, m_msaaView,  nullptr));
    SAFE_VK(m_msaaImage, vkDestroyImage    (m_device, m_msaaImage, nullptr));
    SAFE_VK(m_msaaMem,   vkFreeMemory      (m_device, m_msaaMem,   nullptr));
}

bool VulkanSVGRenderer::createRenderPass() {
    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);

    VkAttachmentDescription dsAttach = {};
    dsAttach.format         = m_dsFormat;
    dsAttach.samples        = m_msaaSamples;
    dsAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    dsAttach.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dsAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    dsAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dsAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    dsAttach.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDependency selfDep = {};
    selfDep.srcSubpass      = 0;
    selfDep.dstSubpass      = 0;
    selfDep.srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    selfDep.dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    selfDep.srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    selfDep.dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    selfDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    if(!msaa) {
        RLOG("RenderPass: single-sample (colour + DS)");
        VkAttachmentDescription attachments[2] = {};

        attachments[0].format         = m_swapFormat;
        attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1] = dsAttach;

        VkAttachmentReference colRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference dsRef  = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub = {};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colRef;
        sub.pDepthStencilAttachment = &dsRef;

        VkSubpassDependency extDep = {};
        extDep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        extDep.dstSubpass    = 0;
        extDep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        extDep.srcAccessMask = 0;
        extDep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        extDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency deps[] = { extDep, selfDep };

        VkRenderPassCreateInfo ci = {};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2;
        ci.pAttachments    = attachments;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        ci.dependencyCount = 2;
        ci.pDependencies   = deps;
        VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass), "vkCreateRenderPass");
        return true;
    }

    RLOG("RenderPass: MSAA x%d (MS colour + resolve + DS)", (int)m_msaaSamples);
    VkAttachmentDescription attachments[3] = {};

    attachments[0].format         = m_swapFormat;
    attachments[0].samples        = m_msaaSamples;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format         = m_swapFormat;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[2] = dsAttach;

    VkAttachmentReference colorRef   = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference resolveRef = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dsRef      = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub = {};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pResolveAttachments     = &resolveRef;
    sub.pDepthStencilAttachment = &dsRef;

    VkSubpassDependency extDep = {};
    extDep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    extDep.dstSubpass    = 0;
    extDep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    extDep.srcAccessMask = 0;
    extDep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    extDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency deps[] = { extDep, selfDep };

    VkRenderPassCreateInfo ci = {};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 3;
    ci.pAttachments    = attachments;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies   = deps;
    VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass), "vkCreateRenderPass MSAA");
    return true;
}

bool VulkanSVGRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo ci = {};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings    = &uboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_descSetLayout),
             "vkCreateDescriptorSetLayout");
    return true;
}

VkShaderModule VulkanSVGRenderer::compileGLSL(const char* src, bool isVertex) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);
    opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);

    auto result = compiler.CompileGlslToSpv(
        src, strlen(src),
        isVertex ? shaderc_vertex_shader : shaderc_fragment_shader,
        isVertex ? "vert.glsl" : "frag.glsl",
        "main", opts);

    if(result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(std::string("Shader compile error: ") +
                                 result.GetErrorMessage());

    std::vector<uint32_t> spv(result.cbegin(), result.cend());

    VkShaderModuleCreateInfo ci = {};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode    = spv.data();

    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(m_device, &ci, nullptr, &mod), "vkCreateShaderModule");
    return mod;
}

bool VulkanSVGRenderer::createPipeline() {
    VkShaderModule vs = compileGLSL(VS_GLSL, true);
    VkShaderModule fs = compileGLSL(FS_GLSL, false);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding   = 0;
    binding.stride    = sizeof(Mesh::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(Mesh::Vertex, x);
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset   = offsetof(Mesh::Vertex, r);

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    VkPipelineViewportStateCreateInfo vps = {};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = m_msaaSamples;

    VkPipelineColorBlendAttachmentState blendOn = {};
    blendOn.blendEnable         = VK_TRUE;
    blendOn.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendOn.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendOn.colorBlendOp        = VK_BLEND_OP_ADD;
    blendOn.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendOn.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendOn.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendOn.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState blendOff = blendOn;
    blendOff.blendEnable    = VK_FALSE;
    blendOff.colorWriteMask = 0;

    VkPipelineColorBlendStateCreateInfo blendStateOn = {};
    blendStateOn.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendStateOn.attachmentCount = 1;
    blendStateOn.pAttachments    = &blendOn;

    VkPipelineColorBlendStateCreateInfo blendStateOff = blendStateOn;
    blendStateOff.pAttachments = &blendOff;

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_descSetLayout;
    VK_CHECK(vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout),
             "vkCreatePipelineLayout");

    VkPipelineDepthStencilStateCreateInfo dsOff = {};
    dsOff.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkStencilOpState nzFront = {};
    nzFront.failOp      = VK_STENCIL_OP_KEEP;
    nzFront.passOp      = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    nzFront.depthFailOp = VK_STENCIL_OP_KEEP;
    nzFront.compareOp   = VK_COMPARE_OP_ALWAYS;
    nzFront.writeMask   = 0xFF;
    nzFront.compareMask = 0xFF;
    nzFront.reference   = 0;

    VkStencilOpState nzBack = nzFront;
    nzBack.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;

    VkPipelineDepthStencilStateCreateInfo dsNZ = {};
    dsNZ.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsNZ.stencilTestEnable = VK_TRUE;
    dsNZ.front            = nzFront;
    dsNZ.back             = nzBack;

    VkStencilOpState eoState = {};
    eoState.failOp      = VK_STENCIL_OP_KEEP;
    eoState.passOp      = VK_STENCIL_OP_INVERT;
    eoState.depthFailOp = VK_STENCIL_OP_KEEP;
    eoState.compareOp   = VK_COMPARE_OP_ALWAYS;
    eoState.writeMask   = 0xFF;
    eoState.compareMask = 0xFF;
    eoState.reference   = 0;

    VkPipelineDepthStencilStateCreateInfo dsEO = {};
    dsEO.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsEO.stencilTestEnable = VK_TRUE;
    dsEO.front             = eoState;
    dsEO.back              = eoState;

    VkStencilOpState readState = {};
    readState.failOp      = VK_STENCIL_OP_KEEP;
    readState.passOp      = VK_STENCIL_OP_REPLACE;
    readState.depthFailOp = VK_STENCIL_OP_KEEP;
    readState.compareOp   = VK_COMPARE_OP_NOT_EQUAL;
    readState.writeMask   = 0xFF;
    readState.compareMask = 0xFF;
    readState.reference   = 0;

    VkPipelineDepthStencilStateCreateInfo dsRead = {};
    dsRead.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsRead.stencilTestEnable = VK_TRUE;
    dsRead.front             = readState;
    dsRead.back              = readState;

    VkGraphicsPipelineCreateInfo base = {};
    base.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    base.stageCount          = 2;
    base.pStages             = stages;
    base.pVertexInputState   = &vi;
    base.pInputAssemblyState = &ia;
    base.pViewportState      = &vps;
    base.pRasterizationState = &rs;
    base.pMultisampleState   = &ms;
    base.pDynamicState       = &dyn;
    base.layout              = m_pipelineLayout;
    base.renderPass          = m_renderPass;

    VkGraphicsPipelineCreateInfo infos[4] = { base, base, base, base };
    infos[0].pDepthStencilState = &dsOff;
    infos[0].pColorBlendState   = &blendStateOn;
    infos[1].pDepthStencilState = &dsNZ;
    infos[1].pColorBlendState   = &blendStateOff;
    infos[2].pDepthStencilState = &dsEO;
    infos[2].pColorBlendState   = &blendStateOff;
    infos[3].pDepthStencilState = &dsRead;
    infos[3].pColorBlendState   = &blendStateOn;

    VkPipeline pipes[4] = {};
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 4, infos, nullptr, pipes),
             "vkCreateGraphicsPipelines");

    m_pipeline            = pipes[0];
    m_stencilNZPipeline   = pipes[1];
    m_stencilEOPipeline   = pipes[2];
    m_stencilReadPipeline = pipes[3];
    RLOG("Pipelines created: main + stencilNZ + stencilEO + stencilRead");

    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, fs, nullptr);
    return true;
}

bool VulkanSVGRenderer::createFramebuffers() {
    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);
    m_framebuffers.resize(m_swapImageViews.size());
    RLOG("Framebuffers: count=%d  msaa=%d", (int)m_swapImageViews.size(), (int)msaa);
    for(size_t i = 0; i < m_swapImageViews.size(); i++) {
        VkImageView fbAttachments[3];
        uint32_t    attachCount;
        if(msaa) {
            fbAttachments[0] = m_msaaView;
            fbAttachments[1] = m_swapImageViews[i];
            fbAttachments[2] = m_dsView;
            attachCount = 3;
        } else {
            fbAttachments[0] = m_swapImageViews[i];
            fbAttachments[1] = m_dsView;
            attachCount = 2;
        }

        VkFramebufferCreateInfo ci = {};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_renderPass;
        ci.attachmentCount = attachCount;
        ci.pAttachments    = fbAttachments;
        ci.width           = m_swapExtent.width;
        ci.height          = m_swapExtent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(m_device, &ci, nullptr, &m_framebuffers[i]),
                 "vkCreateFramebuffer");
    }
    return true;
}

bool VulkanSVGRenderer::createCommandPool() {
    VkCommandPoolCreateInfo ci = {};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = m_graphicsFamily;
    VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool), "vkCreateCommandPool");
    return true;
}

bool VulkanSVGRenderer::createUniformBuffers() {
    size_t n = m_swapImages.size();
    m_uboBufs.resize(n);
    m_uboMems.resize(n);
    m_uboMapped.resize(n);

    for(size_t i = 0; i < n; i++) {
        createBuffer(sizeof(Mat4),
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_uboBufs[i], m_uboMems[i]);
        vkMapMemory(m_device, m_uboMems[i], 0, sizeof(Mat4), 0, &m_uboMapped[i]);
    }
    return true;
}

bool VulkanSVGRenderer::createDescriptorPool() {
    uint32_t n = (uint32_t)m_swapImages.size();
    VkDescriptorPoolSize ps = {};
    ps.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = n;

    VkDescriptorPoolCreateInfo ci = {};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = n;
    ci.poolSizeCount = 1;
    ci.pPoolSizes    = &ps;
    VK_CHECK(vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descPool),
             "vkCreateDescriptorPool");
    return true;
}

bool VulkanSVGRenderer::createDescriptorSets() {
    uint32_t n = (uint32_t)m_swapImages.size();
    std::vector<VkDescriptorSetLayout> layouts(n, m_descSetLayout);

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = n;
    ai.pSetLayouts        = layouts.data();

    m_descSets.resize(n);
    VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, m_descSets.data()),
             "vkAllocateDescriptorSets");

    for(uint32_t i = 0; i < n; i++) {
        VkDescriptorBufferInfo bi = {};
        bi.buffer = m_uboBufs[i];
        bi.offset = 0;
        bi.range  = sizeof(Mat4);

        VkWriteDescriptorSet wr = {};
        wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet          = m_descSets[i];
        wr.dstBinding      = 0;
        wr.descriptorCount = 1;
        wr.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        wr.pBufferInfo     = &bi;
        vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);
    }
    return true;
}

bool VulkanSVGRenderer::createCommandBuffers() {
    m_cmdBuffers.resize(m_swapImages.size());
    VkCommandBufferAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)m_cmdBuffers.size();
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, m_cmdBuffers.data()),
             "vkAllocateCommandBuffers");
    return true;
}

bool VulkanSVGRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for(uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateSemaphore(m_device, &sci, nullptr, &m_imageAvailSem[i]), "imageAvailSem");
        VK_CHECK(vkCreateFence    (m_device, &fci, nullptr, &m_frameFence[i]),    "frameFence");
    }

    m_renderDoneSem.resize(m_swapImages.size());
    for(size_t i = 0; i < m_swapImages.size(); i++) {
        VK_CHECK(vkCreateSemaphore(m_device, &sci, nullptr, &m_renderDoneSem[i]), "renderDoneSem");
    }

    m_imageInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    m_timestampPeriod = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo qpci = {};
    qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = 2;

    for(uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateQueryPool(m_device, &qpci, nullptr, &m_queryPools[i]), "vkCreateQueryPool");
    }

    return true;
}

void VulkanSVGRenderer::cleanupSwapchain() {
    cleanupDepthStencilResources();
    cleanupMSAAResources();

    for(auto fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    if(!m_cmdBuffers.empty()) {
        vkFreeCommandBuffers(m_device, m_cmdPool,
                             (uint32_t)m_cmdBuffers.size(), m_cmdBuffers.data());
        m_cmdBuffers.clear();
    }

    SAFE_VK(m_stencilReadPipeline, vkDestroyPipeline(m_device, m_stencilReadPipeline, nullptr));
    SAFE_VK(m_stencilEOPipeline,   vkDestroyPipeline(m_device, m_stencilEOPipeline,   nullptr));
    SAFE_VK(m_stencilNZPipeline,   vkDestroyPipeline(m_device, m_stencilNZPipeline,   nullptr));
    SAFE_VK(m_pipeline,            vkDestroyPipeline(m_device, m_pipeline,            nullptr));
    SAFE_VK(m_pipelineLayout,      vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr));
    SAFE_VK(m_renderPass,          vkDestroyRenderPass(m_device, m_renderPass, nullptr));

    for(auto iv : m_swapImageViews) vkDestroyImageView(m_device, iv, nullptr);
    m_swapImageViews.clear();

    for(auto sem : m_renderDoneSem) vkDestroySemaphore(m_device, sem, nullptr);
    m_renderDoneSem.clear();

    for(size_t i = 0; i < m_uboBufs.size(); i++) {
        if(m_uboMapped[i]) { vkUnmapMemory(m_device, m_uboMems[i]); m_uboMapped[i] = nullptr; }
        vkDestroyBuffer(m_device, m_uboBufs[i], nullptr);
        vkFreeMemory   (m_device, m_uboMems[i], nullptr);
    }
    m_uboBufs.clear(); m_uboMems.clear(); m_uboMapped.clear();

    SAFE_VK(m_descPool, vkDestroyDescriptorPool(m_device, m_descPool, nullptr));
    m_descSets.clear();

    SAFE_VK(m_swapchain, vkDestroySwapchainKHR(m_device, m_swapchain, nullptr));
    m_swapImages.clear();
}

void VulkanSVGRenderer::recreateSwapchain() {
    RLOG("recreateSwapchain");
    vkDeviceWaitIdle(m_device);
    cleanupSwapchain();
    createSwapchain();
    createDepthStencilResources();
    createMSAAResources();
    createRenderPass();
    createPipeline();
    createFramebuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();

    m_renderDoneSem.resize(m_swapImages.size());
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for(size_t i = 0; i < m_swapImages.size(); i++) {
        vkCreateSemaphore(m_device, &sci, nullptr, &m_renderDoneSem[i]);
    }
    m_imageInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
}

uint32_t VulkanSVGRenderer::findMemoryType(uint32_t filter,
                                            VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &mp);
    for(uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if((filter & (1u << i)) &&
           (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("findMemoryType: no suitable type");
}

bool VulkanSVGRenderer::createBuffer(VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags props,
                                      VkBuffer& outBuf, VkDeviceMemory& outMem) {
    VkBufferCreateInfo bi = {};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_device, &bi, nullptr, &outBuf), "vkCreateBuffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, outBuf, &mr);

    VkMemoryAllocateInfo ai = {};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &outMem), "vkAllocateMemory");
    vkBindBufferMemory(m_device, outBuf, outMem, 0);
    return true;
}

void VulkanSVGRenderer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai = {};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = m_cmdPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region = { 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);
}

bool VulkanSVGRenderer::uploadVertexBuffer(const std::vector<Mesh::Vertex>& verts) {
    VkDeviceSize size = verts.size() * sizeof(Mesh::Vertex);

    VkBuffer stageBuf; VkDeviceMemory stageMem;
    createBuffer(size,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void* data;
    vkMapMemory(m_device, stageMem, 0, size, 0, &data);
    memcpy(data, verts.data(), (size_t)size);
    vkUnmapMemory(m_device, stageMem);

    createBuffer(size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_vertexBuf, m_vertexMem);
    copyBuffer(stageBuf, m_vertexBuf, size);

    vkDestroyBuffer(m_device, stageBuf, nullptr);
    vkFreeMemory   (m_device, stageMem, nullptr);
    return true;
}

bool VulkanSVGRenderer::uploadIndexBuffer(const std::vector<uint32_t>& indices) {
    VkDeviceSize size = indices.size() * sizeof(uint32_t);

    VkBuffer stageBuf; VkDeviceMemory stageMem;
    createBuffer(size,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void* data;
    vkMapMemory(m_device, stageMem, 0, size, 0, &data);
    memcpy(data, indices.data(), (size_t)size);
    vkUnmapMemory(m_device, stageMem);

    createBuffer(size,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_indexBuf, m_indexMem);
    copyBuffer(stageBuf, m_indexBuf, size);

    vkDestroyBuffer(m_device, stageBuf, nullptr);
    vkFreeMemory   (m_device, stageMem, nullptr);
    return true;
}

bool VulkanSVGRenderer::uploadStencilBuffers(const std::vector<Mesh::Vertex>& verts,
                                              const std::vector<uint32_t>& indices) {
    if(verts.empty() || indices.empty()) return true;

    {
        VkDeviceSize size = verts.size() * sizeof(Mesh::Vertex);
        VkBuffer stageBuf; VkDeviceMemory stageMem;
        createBuffer(size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stageBuf, stageMem);
        void* data;
        vkMapMemory(m_device, stageMem, 0, size, 0, &data);
        memcpy(data, verts.data(), (size_t)size);
        vkUnmapMemory(m_device, stageMem);

        createBuffer(size,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_stencilVertBuf, m_stencilVertMem);
        copyBuffer(stageBuf, m_stencilVertBuf, size);
        vkDestroyBuffer(m_device, stageBuf, nullptr);
        vkFreeMemory   (m_device, stageMem,  nullptr);
    }

    {
        VkDeviceSize size = indices.size() * sizeof(uint32_t);
        VkBuffer stageBuf; VkDeviceMemory stageMem;
        createBuffer(size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stageBuf, stageMem);
        void* data;
        vkMapMemory(m_device, stageMem, 0, size, 0, &data);
        memcpy(data, indices.data(), (size_t)size);
        vkUnmapMemory(m_device, stageMem);

        createBuffer(size,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_stencilIdxBuf, m_stencilIdxMem);
        copyBuffer(stageBuf, m_stencilIdxBuf, size);
        vkDestroyBuffer(m_device, stageBuf, nullptr);
        vkFreeMemory   (m_device, stageMem,  nullptr);
    }
    return true;
}

void VulkanSVGRenderer::destroyGeometryBuffers() {
    if(!m_device) return;
    SAFE_VK(m_vertexBuf,      vkDestroyBuffer(m_device, m_vertexBuf,      nullptr));
    SAFE_VK(m_vertexMem,      vkFreeMemory   (m_device, m_vertexMem,      nullptr));
    SAFE_VK(m_indexBuf,       vkDestroyBuffer(m_device, m_indexBuf,       nullptr));
    SAFE_VK(m_indexMem,       vkFreeMemory   (m_device, m_indexMem,       nullptr));
    SAFE_VK(m_stencilVertBuf, vkDestroyBuffer(m_device, m_stencilVertBuf, nullptr));
    SAFE_VK(m_stencilVertMem, vkFreeMemory   (m_device, m_stencilVertMem, nullptr));
    SAFE_VK(m_stencilIdxBuf,  vkDestroyBuffer(m_device, m_stencilIdxBuf,  nullptr));
    SAFE_VK(m_stencilIdxMem,  vkFreeMemory   (m_device, m_stencilIdxMem,  nullptr));
    m_indexCount = 0;
    m_stencilDraws.clear();
}

void VulkanSVGRenderer::loadMesh(const Mesh& mesh) {
    auto startTime = std::chrono::high_resolution_clock::now();

    if(m_device) vkDeviceWaitIdle(m_device);
    destroyGeometryBuffers();
    RLOG("loadMesh: %d vertices  %d indices (%d tris)  %d stencilFills",
         (int)mesh.vertices.size(),
         (int)mesh.indices.size(),
         (int)mesh.indices.size() / 3,
         (int)mesh.stencilFills.size());

    if(!mesh.vertices.empty() && !mesh.indices.empty()) {
        uploadVertexBuffer(mesh.vertices);
        uploadIndexBuffer(mesh.indices);
        m_indexCount = (uint32_t)mesh.indices.size();
    }

    if(!mesh.stencilFills.empty()) {
        std::vector<Mesh::Vertex> flatVerts;
        std::vector<uint32_t>     flatIdx;
        m_stencilDraws.clear();

        for(auto& sf : mesh.stencilFills) {
            StencilFillDraw sd;
            sd.vertexOffset  = (int32_t)flatVerts.size();
            sd.fanFirstIndex = (uint32_t)flatIdx.size();

            uint32_t bboxIdxStart = (uint32_t)(sf.indices.size() - 6);
            sd.fanIndexCount  = bboxIdxStart;
            sd.bboxFirstIndex = sd.fanFirstIndex + bboxIdxStart;
            sd.evenOdd        = sf.evenOdd;

            RLOG("  stencilFill: evenOdd=%d  verts=%d  fanIdxCount=%d  vertexOffset=%d",
                 (int)sd.evenOdd,
                 (int)sf.verts.size(),
                 (int)sd.fanIndexCount,
                 (int)sd.vertexOffset);

            for(auto& v : sf.verts)    flatVerts.push_back(v);
            for(auto  i : sf.indices)  flatIdx.push_back(i);

            m_stencilDraws.push_back(sd);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        RLOG("stencilFills total: %d fills  %d verts  %d indices. Upload in %.2f ms",
             (int)m_stencilDraws.size(),
             (int)flatVerts.size(),
             (int)flatIdx.size(),
             ms);
        uploadStencilBuffers(flatVerts, flatIdx);
    }
}

void VulkanSVGRenderer::loadDocument(const SVGDocument& doc) {
    m_svgW = doc.viewport.w;
    m_svgH = doc.viewport.h;
    RLOG("loadDocument: svgW=%.0f svgH=%.0f  shapes=%d",
         m_svgW, m_svgH, (int)doc.shapes.size());
    loadMesh(tessellateDocument(doc));
}

void VulkanSVGRenderer::loadSVGString(const std::string& svg) {
    loadDocument(parseSVG(svg));
}

void VulkanSVGRenderer::resize(int width, int height) {
    if(width == m_width && height == m_height) return;
    RLOG("\n[RNDR] resize: %dx%d -> %dx%d", m_width, m_height, width, height);
    m_width  = width;
    m_height = height;
    m_needsResize = true;
}

void VulkanSVGRenderer::updateUBO(uint32_t imageIndex,
                                   float /*bgR*/, float /*bgG*/, float /*bgB*/) {
    Mat4 proj = ortho2D(0.f, m_svgW, m_svgH, 0.f);
    memcpy(m_uboMapped[imageIndex], &proj, sizeof(proj));
}

void VulkanSVGRenderer::render(float bgR, float bgG, float bgB) {
    auto cpuStart = std::chrono::high_resolution_clock::now();

    if(m_needsResize) {
        recreateSwapchain();
        m_needsResize = false;
    }

    vkWaitForFences(m_device, 1, &m_frameFence[m_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_imageAvailSem[m_currentFrame], VK_NULL_HANDLE,
        &m_currentImageIndex);

    if(result == VK_ERROR_OUT_OF_DATE_KHR) {
        RLOG("vkAcquireNextImageKHR: OUT_OF_DATE — recreating swapchain");
        recreateSwapchain();
        return;
    }
    if(result == VK_SUBOPTIMAL_KHR)
        RLOG("vkAcquireNextImageKHR: SUBOPTIMAL (continuing)");

    if(m_imageInFlight[m_currentImageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(m_device, 1, &m_imageInFlight[m_currentImageIndex], VK_TRUE, UINT64_MAX);
    m_imageInFlight[m_currentImageIndex] = m_frameFence[m_currentFrame];

    updateUBO(m_currentImageIndex, bgR, bgG, bgB);

    VkCommandBuffer cmd = m_cmdBuffers[m_currentImageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    vkCmdResetQueryPool(cmd, m_queryPools[m_currentFrame], 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPools[m_currentFrame], 0);

    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);
    VkClearValue clears[3] = {};
    if(msaa) {
        clears[0].color          = {{ bgR, bgG, bgB, 1.f }};
        clears[2].depthStencil   = { 1.0f, 0 };
    } else {
        clears[0].color          = {{ bgR, bgG, bgB, 1.f }};
        clears[1].depthStencil   = { 1.0f, 0 };
    }

    VkRenderPassBeginInfo rbi = {};
    rbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass        = m_renderPass;
    rbi.framebuffer       = m_framebuffers[m_currentImageIndex];
    rbi.renderArea.extent = m_swapExtent;
    rbi.clearValueCount   = msaa ? 3u : 2u;
    rbi.pClearValues      = clears;

    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    float winW = (float)m_swapExtent.width;
    float winH = (float)m_swapExtent.height;
    float svgAspect = (m_svgH > 0.f) ? m_svgW / m_svgH : 1.f;
    float winAspect = (winH   > 0.f) ? winW   / winH   : 1.f;
    float vpW, vpH, vpX, vpY;
    if(winAspect >= svgAspect) {
        vpH = winH;  vpW = winH * svgAspect;
        vpX = (winW - vpW) * 0.5f;  vpY = 0.f;
    } else {
        vpW = winW;  vpH = winW / svgAspect;
        vpX = 0.f;   vpY = (winH - vpH) * 0.5f;
    }
    
    VkViewport vp = {};
    vp.x        = vpX;
    vp.y        = vpY + vpH;
    vp.width    = vpW;
    vp.height   = -vpH;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.offset = { (int32_t)vpX, (int32_t)vpY };
    scissor.extent = { (uint32_t)vpW, (uint32_t)vpH };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if(m_vertexBuf && m_indexBuf && m_indexCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_descSets[m_currentImageIndex], 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuf, &offset);
        vkCmdBindIndexBuffer  (cmd, m_indexBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
    }

    if(m_stencilVertBuf && m_stencilIdxBuf && !m_stencilDraws.empty()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_descSets[m_currentImageIndex], 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_stencilVertBuf, &offset);
        vkCmdBindIndexBuffer  (cmd, m_stencilIdxBuf, 0, VK_INDEX_TYPE_UINT32);

        VkMemoryBarrier stencilBarrier = {};
        stencilBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        stencilBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        stencilBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        for(auto& sd : m_stencilDraws) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                sd.evenOdd ? m_stencilEOPipeline : m_stencilNZPipeline);
            vkCmdDrawIndexed(cmd,
                sd.fanIndexCount, 1,
                sd.fanFirstIndex, sd.vertexOffset, 0);

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_DEPENDENCY_BY_REGION_BIT,
                1, &stencilBarrier,
                0, nullptr,
                0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_stencilReadPipeline);
            vkCmdDrawIndexed(cmd,
                6, 1,
                sd.bboxFirstIndex, sd.vertexOffset, 0);
        }
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPools[m_currentFrame], 1);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &m_imageAvailSem[m_currentFrame];
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_renderDoneSem[m_currentImageIndex];

    vkResetFences(m_device, 1, &m_frameFence[m_currentFrame]);
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, m_frameFence[m_currentFrame]),
             "vkQueueSubmit");

    uint64_t timestamps[2] = {0, 0};
    vkGetQueryPoolResults(m_device, m_queryPools[m_currentFrame], 0, 2, 
                          sizeof(timestamps), timestamps, sizeof(uint64_t), 
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                          
    float gpuMs = (timestamps[1] - timestamps[0]) * m_timestampPeriod * 1e-6f;

    auto cpuEnd = std::chrono::high_resolution_clock::now();
    float cpuMs = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
    float fps = 1000.0f / cpuMs;
    printf("[FRAME] CPU: %.2f ms GPU: %.2f ms FPS: %.1f              \r",
        cpuMs, gpuMs, fps);
}

void VulkanSVGRenderer::present(bool vsync) {
    VkPresentModeKHR desired = vsync
        ? VK_PRESENT_MODE_FIFO_KHR
        : VK_PRESENT_MODE_MAILBOX_KHR;

    VkPresentInfoKHR pi = {};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_renderDoneSem[m_currentImageIndex];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &pi);

    bool outOfDate = (result == VK_ERROR_OUT_OF_DATE_KHR ||
                      result == VK_SUBOPTIMAL_KHR         ||
                      m_needsResize);
    bool vsyncChanged = (desired != m_presentMode);

    if(outOfDate || vsyncChanged) {
        RLOG("present: %s%s - recreating swapchain",
             outOfDate    ? "out-of-date/suboptimal " : "",
             vsyncChanged ? "vsync-changed"           : "");
        m_presentMode = desired;
        recreateSwapchain();
        m_needsResize = false;
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}