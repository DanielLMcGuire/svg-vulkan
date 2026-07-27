#include "renderer.h"
#include "renderer_internal.h"
#include <algorithm>
#include <cassert>

bool VulkanSVGRenderer::createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physDevice, m_surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDevice, m_surface, &fmtCount,
                                         fmts.data());

    VkSurfaceFormatKHR chosenFmt = fmts[0];
    for (auto &f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosenFmt = f;
            break;
        }
    m_swapFormat = chosenFmt.format;

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface,
                                              &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDevice, m_surface,
                                              &modeCount, modes.data());

    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

    if (caps.currentExtent.width != UINT32_MAX)
    {
        m_swapExtent = caps.currentExtent;
    }
    else
    {
        m_swapExtent.width =
            std::max(caps.minImageExtent.width,
                     std::min(caps.maxImageExtent.width, (uint32_t)m_width));
        m_swapExtent.height =
            std::max(caps.minImageExtent.height,
                     std::min(caps.maxImageExtent.height, (uint32_t)m_height));
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        imgCount = std::min(imgCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = m_surface;
    ci.minImageCount = imgCount;
    ci.imageFormat = chosenFmt.format;
    ci.imageColorSpace = chosenFmt.colorSpace;
    ci.imageExtent = m_swapExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = m_presentMode;
    ci.clipped = VK_TRUE;

    if (m_graphicsFamily != m_presentFamily)
    {
        uint32_t families[] = {m_graphicsFamily, m_presentFamily};
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families;
    }
    else
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain),
             "vkCreateSwapchainKHR");

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_swapImages.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount,
                            m_swapImages.data());
    RLOG("Swapchain: %ux%u  format=%d  images=%u  presentMode=%d",
         m_swapExtent.width, m_swapExtent.height, (int)m_swapFormat,
         actualCount, (int)m_presentMode);

    m_swapImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; i++)
    {
        VkImageViewCreateInfo ivci = {};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_swapImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = m_swapFormat;
        ivci.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        VK_CHECK(
            vkCreateImageView(m_device, &ivci, nullptr, &m_swapImageViews[i]),
            "vkCreateImageView");
    }
    return true;
}

VkFormat VulkanSVGRenderer::findDepthStencilFormat()
{
    VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    for (auto fmt : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physDevice, fmt, &props);
        if (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    throw std::runtime_error("No suitable depth/stencil format found");
}

bool VulkanSVGRenderer::createDepthStencilResources()
{
    m_dsFormat = findDepthStencilFormat();
    RLOG("DepthStencil: format=%d  extent=%ux%u  samples=%d", (int)m_dsFormat,
         m_swapExtent.width, m_swapExtent.height, (int)m_msaaSamples);

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = m_dsFormat;
    ici.extent = {m_swapExtent.width, m_swapExtent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = m_msaaSamples;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(m_device, &ici, nullptr, &m_dsImage),
             "DS vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_dsImage, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex =
        findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &m_dsMem),
             "DS vkAllocateMemory");
    vkBindImageMemory(m_device, m_dsImage, m_dsMem, 0);

    VkImageViewCreateInfo ivci = {};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = m_dsImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = m_dsFormat;
    ivci.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ivci.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_dsView),
             "DS vkCreateImageView");
    return true;
}

void VulkanSVGRenderer::cleanupDepthStencilResources()
{
    if (!m_device)
        return;
    SAFE_VK(m_dsView, vkDestroyImageView(m_device, m_dsView, nullptr));
    SAFE_VK(m_dsImage, vkDestroyImage(m_device, m_dsImage, nullptr));
    SAFE_VK(m_dsMem, vkFreeMemory(m_device, m_dsMem, nullptr));
}

VkSampleCountFlagBits VulkanSVGRenderer::getMaxSampleCount()
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    VkSampleCountFlags c = props.limits.framebufferColorSampleCounts &
                           props.limits.framebufferDepthSampleCounts;
    if (c & VK_SAMPLE_COUNT_8_BIT)
        return VK_SAMPLE_COUNT_8_BIT;
    if (c & VK_SAMPLE_COUNT_4_BIT)
        return VK_SAMPLE_COUNT_4_BIT;
    if (c & VK_SAMPLE_COUNT_2_BIT)
        return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

bool VulkanSVGRenderer::createMSAAResources()
{
    if (m_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return true;

    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = m_swapFormat;
    ici.extent = {m_swapExtent.width, m_swapExtent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = m_msaaSamples;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(m_device, &ici, nullptr, &m_msaaImage),
             "MSAA vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_msaaImage, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex =
        findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &m_msaaMem),
             "MSAA vkAllocateMemory");
    vkBindImageMemory(m_device, m_msaaImage, m_msaaMem, 0);

    VkImageViewCreateInfo ivci = {};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = m_msaaImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = m_swapFormat;
    ivci.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_msaaView),
             "MSAA vkCreateImageView");
    return true;
}

void VulkanSVGRenderer::cleanupMSAAResources()
{
    if (!m_device)
        return;
    SAFE_VK(m_msaaView, vkDestroyImageView(m_device, m_msaaView, nullptr));
    SAFE_VK(m_msaaImage, vkDestroyImage(m_device, m_msaaImage, nullptr));
    SAFE_VK(m_msaaMem, vkFreeMemory(m_device, m_msaaMem, nullptr));
}

void VulkanSVGRenderer::cleanupSwapchain()
{
    cleanupDepthStencilResources();
    cleanupMSAAResources();

    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    if (!m_cmdBuffers.empty())
    {
        vkFreeCommandBuffers(m_device, m_cmdPool, (uint32_t)m_cmdBuffers.size(),
                             m_cmdBuffers.data());
        m_cmdBuffers.clear();
    }

    SAFE_VK(m_stencilReadPipeline,
            vkDestroyPipeline(m_device, m_stencilReadPipeline, nullptr));
    SAFE_VK(m_stencilEOPipeline,
            vkDestroyPipeline(m_device, m_stencilEOPipeline, nullptr));
    SAFE_VK(m_stencilNZPipeline,
            vkDestroyPipeline(m_device, m_stencilNZPipeline, nullptr));
    SAFE_VK(m_pipeline, vkDestroyPipeline(m_device, m_pipeline, nullptr));
    SAFE_VK(m_pipelineLayout,
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr));
    SAFE_VK(m_renderPass, vkDestroyRenderPass(m_device, m_renderPass, nullptr));

    for (auto iv : m_swapImageViews)
        vkDestroyImageView(m_device, iv, nullptr);
    m_swapImageViews.clear();

    for (auto sem : m_renderDoneSem)
        vkDestroySemaphore(m_device, sem, nullptr);
    m_renderDoneSem.clear();

    for (size_t i = 0; i < m_uboBufs.size(); i++)
    {
        if (m_uboMapped[i])
        {
            vkUnmapMemory(m_device, m_uboMems[i]);
            m_uboMapped[i] = nullptr;
        }
        vkDestroyBuffer(m_device, m_uboBufs[i], nullptr);
        vkFreeMemory(m_device, m_uboMems[i], nullptr);
    }
    m_uboBufs.clear();
    m_uboMems.clear();
    m_uboMapped.clear();

    SAFE_VK(m_descPool, vkDestroyDescriptorPool(m_device, m_descPool, nullptr));
    m_descSets.clear();

    SAFE_VK(m_swapchain, vkDestroySwapchainKHR(m_device, m_swapchain, nullptr));
    m_swapImages.clear();
}

void VulkanSVGRenderer::recreateSwapchain()
{
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
    VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (size_t i = 0; i < m_swapImages.size(); i++)
    {
        vkCreateSemaphore(m_device, &sci, nullptr, &m_renderDoneSem[i]);
    }
    m_imageInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);
}
