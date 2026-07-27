#include "renderer.h"
#include "renderer_internal.h"
#include <chrono>
#include <cstring>

VulkanSVGRenderer::~VulkanSVGRenderer()
{
    if (m_device)
        vkDeviceWaitIdle(m_device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_queryPools[i] != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(m_device, m_queryPools[i], nullptr);
            m_queryPools[i] = VK_NULL_HANDLE;
        }
    }

    destroyGeometryBuffers();

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        SAFE_VK(m_imageAvailSem[i],
                vkDestroySemaphore(m_device, m_imageAvailSem[i], nullptr));
        SAFE_VK(m_frameFence[i],
                vkDestroyFence(m_device, m_frameFence[i], nullptr));
    }

    cleanupSwapchain();

    SAFE_VK(m_vertShaderModule,
            vkDestroyShaderModule(m_device, m_vertShaderModule, nullptr));
    SAFE_VK(m_fragShaderModule,
            vkDestroyShaderModule(m_device, m_fragShaderModule, nullptr));
    SAFE_VK(m_descSetLayout,
            vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr));
    SAFE_VK(m_cmdPool, vkDestroyCommandPool(m_device, m_cmdPool, nullptr));
    SAFE_VK(m_device, vkDestroyDevice(m_device, nullptr));
    SAFE_VK(m_surface, vkDestroySurfaceKHR(m_instance, m_surface, nullptr));

#ifdef _DEBUG
    if (m_debugMessenger != VK_NULL_HANDLE)
    {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn)
            fn(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    SAFE_VK(m_instance, vkDestroyInstance(m_instance, nullptr));
}

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *pData, void *)
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
bool VulkanSVGRenderer::createSurface(HWND hwnd)
{
    VkWin32SurfaceCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hwnd = hwnd;
    ci.hinstance = GetModuleHandleW(nullptr);
    VK_CHECK(vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &m_surface),
             "vkCreateWin32SurfaceKHR");
    return true;
}

bool VulkanSVGRenderer::createInstance()
{
    VkApplicationInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "SVG Renderer";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_1;

#ifdef _WIN32
    const char *surfaceExt = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    const char *surfaceExt = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif

    const char *exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surfaceExt,
#ifdef _DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    constexpr uint32_t layerCount = 1;
#else
    const char **layers = nullptr;
    constexpr uint32_t layerCount = 0;
#endif

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = layerCount;
    ci.ppEnabledLayerNames = layers;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn)
        fn(m_instance, &dci, nullptr, &m_debugMessenger);
#endif

    return true;
}

bool VulkanSVGRenderer::init(HWND hwnd, int width, int height)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    m_width = width;
    m_height = height;
    RLOG("init: %dx%d", width, height);

    if (!createInstance())
    {
        RLOG("FAILED createInstance");
        return false;
    }
    if (!createSurface(hwnd))
    {
        RLOG("FAILED createSurface");
        return false;
    }
    if (!pickPhysicalDevice())
    {
        RLOG("FAILED pickPhysicalDevice");
        return false;
    }
    if (!createLogicalDevice())
    {
        RLOG("FAILED createLogicalDevice");
        return false;
    }
    initShaders();
    if (!createSwapchain())
    {
        RLOG("FAILED createSwapchain");
        return false;
    }
    if (!createDepthStencilResources())
    {
        RLOG("FAILED createDepthStencilResources");
        return false;
    }
    if (!createMSAAResources())
    {
        RLOG("FAILED createMSAAResources");
        return false;
    }
    if (!createRenderPass())
    {
        RLOG("FAILED createRenderPass");
        return false;
    }
    if (!createDescriptorSetLayout())
    {
        RLOG("FAILED createDescriptorSetLayout");
        return false;
    }
    if (!createPipeline())
    {
        RLOG("FAILED createPipeline");
        return false;
    }
    if (!createFramebuffers())
    {
        RLOG("FAILED createFramebuffers");
        return false;
    }
    if (!createCommandPool())
    {
        RLOG("FAILED createCommandPool");
        return false;
    }
    if (!createUniformBuffers())
    {
        RLOG("FAILED createUniformBuffers");
        return false;
    }
    if (!createDescriptorPool())
    {
        RLOG("FAILED createDescriptorPool");
        return false;
    }
    if (!createDescriptorSets())
    {
        RLOG("FAILED createDescriptorSets");
        return false;
    }
    if (!createCommandBuffers())
    {
        RLOG("FAILED createCommandBuffers");
        return false;
    }
    if (!createSyncObjects())
    {
        RLOG("FAILED createSyncObjects");
        return false;
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    float ms =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    RLOG("init complete in %.2f ms", ms);
    return true;
}
#else
bool VulkanSVGRenderer::createSurface(Display *display, Window window)
{
    VkXlibSurfaceCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    ci.dpy = display;
    ci.window = window;
    VK_CHECK(vkCreateXlibSurfaceKHR(m_instance, &ci, nullptr, &m_surface),
             "vkCreateXlibSurfaceKHR");
    return true;
}

bool VulkanSVGRenderer::createInstance()
{
    VkApplicationInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "SVG Renderer";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_1;

#ifdef _WIN32
    const char *surfaceExt = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    const char *surfaceExt = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif

    const char *exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        surfaceExt,
#ifdef _DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };

#ifdef _DEBUG
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    constexpr uint32_t layerCount = 1;
#else
    const char **layers = nullptr;
    constexpr uint32_t layerCount = 0;
#endif

    VkInstanceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)(sizeof(exts) / sizeof(exts[0]));
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = layerCount;
    ci.ppEnabledLayerNames = layers;
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (fn)
        fn(m_instance, &dci, nullptr, &m_debugMessenger);
#endif

    return true;
}

bool VulkanSVGRenderer::init(Display *display, Window window, int width,
                             int height)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    m_width = width;
    m_height = height;
    RLOG("init (X11): %dx%d", width, height);

    if (!createInstance())
    {
        RLOG("FAILED createInstance");
        return false;
    }
    if (!createSurface(display, window))
    {
        RLOG("FAILED createSurface");
        return false;
    }
    if (!pickPhysicalDevice())
    {
        RLOG("FAILED pickPhysicalDevice");
        return false;
    }
    if (!createLogicalDevice())
    {
        RLOG("FAILED createLogicalDevice");
        return false;
    }
    initShaders();
    if (!createSwapchain())
    {
        RLOG("FAILED createSwapchain");
        return false;
    }
    if (!createDepthStencilResources())
    {
        RLOG("FAILED createDepthStencilResources");
        return false;
    }
    if (!createMSAAResources())
    {
        RLOG("FAILED createMSAAResources");
        return false;
    }
    if (!createRenderPass())
    {
        RLOG("FAILED createRenderPass");
        return false;
    }
    if (!createDescriptorSetLayout())
    {
        RLOG("FAILED createDescriptorSetLayout");
        return false;
    }
    if (!createPipeline())
    {
        RLOG("FAILED createPipeline");
        return false;
    }
    if (!createFramebuffers())
    {
        RLOG("FAILED createFramebuffers");
        return false;
    }
    if (!createCommandPool())
    {
        RLOG("FAILED createCommandPool");
        return false;
    }
    if (!createUniformBuffers())
    {
        RLOG("FAILED createUniformBuffers");
        return false;
    }
    if (!createDescriptorPool())
    {
        RLOG("FAILED createDescriptorPool");
        return false;
    }
    if (!createDescriptorSets())
    {
        RLOG("FAILED createDescriptorSets");
        return false;
    }
    if (!createCommandBuffers())
    {
        RLOG("FAILED createCommandBuffers");
        return false;
    }
    if (!createSyncObjects())
    {
        RLOG("FAILED createSyncObjects");
        return false;
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    float ms =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    RLOG("init complete in %.2f ms", ms);
    return true;
}
#endif

bool VulkanSVGRenderer::pickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

    for (auto dev : devs)
    {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());

        uint32_t gfx = UINT32_MAX, pres = UINT32_MAX;
        for (uint32_t i = 0; i < qcount; i++)
        {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                gfx = i;
            VkBool32 ps = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &ps);
            if (ps)
                pres = i;
        }
        if (gfx == UINT32_MAX || pres == UINT32_MAX)
            continue;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount,
                                             exts.data());
        bool hasSwapchain = false;
        for (auto &e : exts)
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            {
                hasSwapchain = true;
                break;
            }
        if (!hasSwapchain)
            continue;

        uint32_t fmtCount = 0, modeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_surface, &fmtCount,
                                             nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, m_surface, &modeCount,
                                                  nullptr);
        if (fmtCount == 0 || modeCount == 0)
            continue;

        m_physDevice = dev;
        m_graphicsFamily = gfx;
        m_presentFamily = pres;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        RLOG("GPU candidate: '%s'  type=%d  gfxQ=%u presQ=%u", props.deviceName,
             (int)props.deviceType, gfx, pres);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            break;
    }

    if (m_physDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable Vulkan device found");
    m_msaaSamples = getMaxSampleCount();
    RLOG("Selected GPU: graphicsFamily=%u presentFamily=%u msaaSamples=%d",
         m_graphicsFamily, m_presentFamily, (int)m_msaaSamples);
    VkPhysicalDeviceDriverProperties d{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 p{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &d};
    vkGetPhysicalDeviceProperties2(m_physDevice, &p);
    RLOG("Using driver: %s", d.driverInfo);
    return true;
}

bool VulkanSVGRenderer::createLogicalDevice()
{
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;

    auto addQueue = [&](uint32_t family)
    {
        VkDeviceQueueCreateInfo qi = {};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        qcis.push_back(qi);
    };
    addQueue(m_graphicsFamily);
    if (m_presentFamily != m_graphicsFamily)
        addQueue(m_presentFamily);

    const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = (uint32_t)qcis.size();
    ci.pQueueCreateInfos = qcis.data();
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = devExts;
    ci.pEnabledFeatures = &features;
    VK_CHECK(vkCreateDevice(m_physDevice, &ci, nullptr, &m_device),
             "vkCreateDevice");

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    return true;
}
