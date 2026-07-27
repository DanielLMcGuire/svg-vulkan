#include "renderer.h"
#include "renderer_internal.h"
#include <cstring>

bool VulkanSVGRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = m_graphicsFamily;
    VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool),
             "vkCreateCommandPool");
    return true;
}

bool VulkanSVGRenderer::createUniformBuffers()
{
    size_t n = m_swapImages.size();
    m_uboBufs.resize(n);
    m_uboMems.resize(n);
    m_uboMapped.resize(n);

    for (size_t i = 0; i < n; i++)
    {
        createBuffer(sizeof(Mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_uboBufs[i], m_uboMems[i]);
        vkMapMemory(m_device, m_uboMems[i], 0, sizeof(Mat4), 0,
                    &m_uboMapped[i]);
    }
    return true;
}

bool VulkanSVGRenderer::createDescriptorPool()
{
    uint32_t n = (uint32_t)m_swapImages.size();
    VkDescriptorPoolSize ps = {};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = n;

    VkDescriptorPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets = n;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &ps;
    VK_CHECK(vkCreateDescriptorPool(m_device, &ci, nullptr, &m_descPool),
             "vkCreateDescriptorPool");
    return true;
}

bool VulkanSVGRenderer::createDescriptorSets()
{
    uint32_t n = (uint32_t)m_swapImages.size();
    std::vector<VkDescriptorSetLayout> layouts(n, m_descSetLayout);

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = n;
    ai.pSetLayouts = layouts.data();

    m_descSets.resize(n);
    VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, m_descSets.data()),
             "vkAllocateDescriptorSets");

    for (uint32_t i = 0; i < n; i++)
    {
        VkDescriptorBufferInfo bi = {};
        bi.buffer = m_uboBufs[i];
        bi.offset = 0;
        bi.range = sizeof(Mat4);

        VkWriteDescriptorSet wr = {};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = m_descSets[i];
        wr.dstBinding = 0;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        wr.pBufferInfo = &bi;
        vkUpdateDescriptorSets(m_device, 1, &wr, 0, nullptr);
    }
    return true;
}

bool VulkanSVGRenderer::createCommandBuffers()
{
    m_cmdBuffers.resize(m_swapImages.size());
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)m_cmdBuffers.size();
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, m_cmdBuffers.data()),
             "vkAllocateCommandBuffers");
    return true;
}

bool VulkanSVGRenderer::createSyncObjects()
{
    VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(
            vkCreateSemaphore(m_device, &sci, nullptr, &m_imageAvailSem[i]),
            "imageAvailSem");
        VK_CHECK(vkCreateFence(m_device, &fci, nullptr, &m_frameFence[i]),
                 "frameFence");
    }

    m_renderDoneSem.resize(m_swapImages.size());
    for (size_t i = 0; i < m_swapImages.size(); i++)
    {
        VK_CHECK(
            vkCreateSemaphore(m_device, &sci, nullptr, &m_renderDoneSem[i]),
            "renderDoneSem");
    }

    m_imageInFlight.assign(m_swapImages.size(), VK_NULL_HANDLE);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physDevice, &props);
    m_timestampPeriod = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo qpci = {};
    qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = 2;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateQueryPool(m_device, &qpci, nullptr, &m_queryPools[i]),
                 "vkCreateQueryPool");
    }

    return true;
}

uint32_t VulkanSVGRenderer::findMemoryType(uint32_t filter,
                                           VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("findMemoryType: no suitable type");
}

bool VulkanSVGRenderer::createBuffer(VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags props,
                                     VkBuffer &outBuf, VkDeviceMemory &outMem)
{
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_device, &bi, nullptr, &outBuf), "vkCreateBuffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, outBuf, &mr);

    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, &outMem),
             "vkAllocateMemory");
    vkBindBufferMemory(m_device, outBuf, outMem, 0);
    return true;
}

void VulkanSVGRenderer::copyBuffer(VkBuffer src, VkBuffer dst,
                                   VkDeviceSize size)
{
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool = m_cmdPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region = {0, 0, size};
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);
}

bool VulkanSVGRenderer::uploadVertexBuffer(
    const std::vector<Mesh::Vertex> &verts)
{
    VkDeviceSize size = verts.size() * sizeof(Mesh::Vertex);

    VkBuffer stageBuf;
    VkDeviceMemory stageMem;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void *data;
    vkMapMemory(m_device, stageMem, 0, size, 0, &data);
    memcpy(data, verts.data(), (size_t)size);
    vkUnmapMemory(m_device, stageMem);

    createBuffer(size,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuf, m_vertexMem);
    copyBuffer(stageBuf, m_vertexBuf, size);

    vkDestroyBuffer(m_device, stageBuf, nullptr);
    vkFreeMemory(m_device, stageMem, nullptr);
    return true;
}

bool VulkanSVGRenderer::uploadIndexBuffer(const std::vector<uint32_t> &indices)
{
    VkDeviceSize size = indices.size() * sizeof(uint32_t);

    VkBuffer stageBuf;
    VkDeviceMemory stageMem;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void *data;
    vkMapMemory(m_device, stageMem, 0, size, 0, &data);
    memcpy(data, indices.data(), (size_t)size);
    vkUnmapMemory(m_device, stageMem);

    createBuffer(size,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuf, m_indexMem);
    copyBuffer(stageBuf, m_indexBuf, size);

    vkDestroyBuffer(m_device, stageBuf, nullptr);
    vkFreeMemory(m_device, stageMem, nullptr);
    return true;
}

bool VulkanSVGRenderer::uploadStencilBuffers(
    const std::vector<Mesh::Vertex> &verts,
    const std::vector<uint32_t> &indices)
{
    if (verts.empty() || indices.empty())
        return true;

    {
        VkDeviceSize size = verts.size() * sizeof(Mesh::Vertex);
        VkBuffer stageBuf;
        VkDeviceMemory stageMem;
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stageBuf, stageMem);
        void *data;
        vkMapMemory(m_device, stageMem, 0, size, 0, &data);
        memcpy(data, verts.data(), (size_t)size);
        vkUnmapMemory(m_device, stageMem);

        createBuffer(size,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_stencilVertBuf,
                     m_stencilVertMem);
        copyBuffer(stageBuf, m_stencilVertBuf, size);
        vkDestroyBuffer(m_device, stageBuf, nullptr);
        vkFreeMemory(m_device, stageMem, nullptr);
    }

    {
        VkDeviceSize size = indices.size() * sizeof(uint32_t);
        VkBuffer stageBuf;
        VkDeviceMemory stageMem;
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stageBuf, stageMem);
        void *data;
        vkMapMemory(m_device, stageMem, 0, size, 0, &data);
        memcpy(data, indices.data(), (size_t)size);
        vkUnmapMemory(m_device, stageMem);

        createBuffer(size,
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_stencilIdxBuf,
                     m_stencilIdxMem);
        copyBuffer(stageBuf, m_stencilIdxBuf, size);
        vkDestroyBuffer(m_device, stageBuf, nullptr);
        vkFreeMemory(m_device, stageMem, nullptr);
    }
    return true;
}

void VulkanSVGRenderer::destroyGeometryBuffers()
{
    if (!m_device)
        return;
    SAFE_VK(m_vertexBuf, vkDestroyBuffer(m_device, m_vertexBuf, nullptr));
    SAFE_VK(m_vertexMem, vkFreeMemory(m_device, m_vertexMem, nullptr));
    SAFE_VK(m_indexBuf, vkDestroyBuffer(m_device, m_indexBuf, nullptr));
    SAFE_VK(m_indexMem, vkFreeMemory(m_device, m_indexMem, nullptr));
    SAFE_VK(m_stencilVertBuf,
            vkDestroyBuffer(m_device, m_stencilVertBuf, nullptr));
    SAFE_VK(m_stencilVertMem,
            vkFreeMemory(m_device, m_stencilVertMem, nullptr));
    SAFE_VK(m_stencilIdxBuf,
            vkDestroyBuffer(m_device, m_stencilIdxBuf, nullptr));
    SAFE_VK(m_stencilIdxMem, vkFreeMemory(m_device, m_stencilIdxMem, nullptr));
    m_indexCount = 0;
    m_stencilDraws.clear();
}
