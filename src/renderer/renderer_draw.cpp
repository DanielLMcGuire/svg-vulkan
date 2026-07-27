#include "renderer.h"
#include "renderer_internal.h"
#include "svg_parser.h"
#include "tessellator.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

static Mat4 ortho2D(float l, float r, float b, float t)
{
    Mat4 M;
    M.m[0][0] = 2.f / (r - l);
    M.m[1][1] = 2.f / (t - b);
    M.m[2][2] = 1.f;
    M.m[3][0] = -(r + l) / (r - l);
    M.m[3][1] = -(t + b) / (t - b);
    M.m[3][3] = 1.f;
    return M;
}

void VulkanSVGRenderer::loadMesh(const Mesh &mesh)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    if (m_device)
        vkDeviceWaitIdle(m_device);
    destroyGeometryBuffers();
    RLOG("loadMesh: %d vertices  %d indices (%d tris)  %d stencilFills",
         (int)mesh.vertices.size(), (int)mesh.indices.size(),
         (int)mesh.indices.size() / 3, (int)mesh.stencilFills.size());

    if (!mesh.vertices.empty() && !mesh.indices.empty())
    {
        uploadVertexBuffer(mesh.vertices);
        uploadIndexBuffer(mesh.indices);
        m_indexCount = (uint32_t)mesh.indices.size();
    }

    if (!mesh.stencilFills.empty())
    {
        std::vector<Mesh::Vertex> flatVerts;
        std::vector<uint32_t> flatIdx;
        m_stencilDraws.clear();

        for (auto &sf : mesh.stencilFills)
        {
            StencilFillDraw sd;
            sd.vertexOffset = (int32_t)flatVerts.size();
            sd.fanFirstIndex = (uint32_t)flatIdx.size();

            uint32_t bboxIdxStart = (uint32_t)(sf.indices.size() - 6);
            sd.fanIndexCount = bboxIdxStart;
            sd.bboxFirstIndex = sd.fanFirstIndex + bboxIdxStart;
            sd.evenOdd = sf.evenOdd;

            RLOG("stencilFill: evenOdd=%d  verts=%d  fanIdxCount=%d  "
                 "vertexOffset=%d",
                 (int)sd.evenOdd, (int)sf.verts.size(), (int)sd.fanIndexCount,
                 (int)sd.vertexOffset);

            for (auto &v : sf.verts)
                flatVerts.push_back(v);
            for (auto i : sf.indices)
                flatIdx.push_back(i);

            m_stencilDraws.push_back(sd);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(endTime - startTime)
                       .count();

        RLOG("stencilFills total: %d fills  %d verts  %d indices. Upload in "
             "%.2f ms",
             (int)m_stencilDraws.size(), (int)flatVerts.size(),
             (int)flatIdx.size(), ms);
        uploadStencilBuffers(flatVerts, flatIdx);
    }
}

void VulkanSVGRenderer::loadDocument(const SVGDocument &doc)
{
    m_svgW = doc.viewport.w;
    m_svgH = doc.viewport.h;
    m_svgTitle = doc.title;
    m_svgPath = doc.path;
    RLOG("loadDocument: svgW=%.0f svgH=%.0f  shapes=%d  title='%s'", m_svgW,
         m_svgH, (int)doc.shapes.size(), m_svgTitle.c_str());
    loadMesh(tessellateDocument(doc));
}

void VulkanSVGRenderer::loadSVGString(const std::string &svg,
                                      const std::string &filePath)
{
    loadDocument(parseSVG(svg, filePath));
}

void VulkanSVGRenderer::resize(int width, int height)
{
    if (width == m_width && height == m_height)
        return;
    RLOG("\n[RNDR] resize: %dx%d -> %dx%d", m_width, m_height, width, height);
    m_width = width;
    m_height = height;
    m_needsResize = true;
}

void VulkanSVGRenderer::updateUBO(uint32_t imageIndex, float /*bgR*/,
                                  float /*bgG*/, float /*bgB*/)
{
    Mat4 proj = ortho2D(0.f, m_svgW, m_svgH, 0.f);
    memcpy(m_uboMapped[imageIndex], &proj, sizeof(proj));
}

void VulkanSVGRenderer::render(float bgR, float bgG, float bgB)
{
    auto cpuStart = std::chrono::high_resolution_clock::now();

    if (m_needsResize)
    {
        recreateSwapchain();
        m_needsResize = false;
    }

    vkWaitForFences(m_device, 1, &m_frameFence[m_currentFrame], VK_TRUE,
                    UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX, m_imageAvailSem[m_currentFrame],
        VK_NULL_HANDLE, &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RLOG("vkAcquireNextImageKHR: OUT_OF_DATE — recreating swapchain");
        recreateSwapchain();
        return;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        RLOG("vkAcquireNextImageKHR: SUBOPTIMAL (continuing)");

    if (m_imageInFlight[m_currentImageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(m_device, 1, &m_imageInFlight[m_currentImageIndex],
                        VK_TRUE, UINT64_MAX);
    m_imageInFlight[m_currentImageIndex] = m_frameFence[m_currentFrame];

    updateUBO(m_currentImageIndex, bgR, bgG, bgB);

    VkCommandBuffer cmd = m_cmdBuffers[m_currentImageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    vkCmdResetQueryPool(cmd, m_queryPools[m_currentFrame], 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        m_queryPools[m_currentFrame], 0);

    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);
    VkClearValue clears[3] = {};
    if (msaa)
    {
        clears[0].color = {{bgR, bgG, bgB, 1.f}};
        clears[2].depthStencil = {1.0f, 0};
    }
    else
    {
        clears[0].color = {{bgR, bgG, bgB, 1.f}};
        clears[1].depthStencil = {1.0f, 0};
    }

    VkRenderPassBeginInfo rbi = {};
    rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass = m_renderPass;
    rbi.framebuffer = m_framebuffers[m_currentImageIndex];
    rbi.renderArea.extent = m_swapExtent;
    rbi.clearValueCount = msaa ? 3u : 2u;
    rbi.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    float winW = (float)m_swapExtent.width;
    float winH = (float)m_swapExtent.height;
    float svgAspect = (m_svgH > 0.f) ? m_svgW / m_svgH : 1.f;
    float winAspect = (winH > 0.f) ? winW / winH : 1.f;
    float vpW, vpH, vpX, vpY;
    if (winAspect >= svgAspect)
    {
        vpH = winH;
        vpW = winH * svgAspect;
        vpX = (winW - vpW) * 0.5f;
        vpY = 0.f;
    }
    else
    {
        vpW = winW;
        vpH = winW / svgAspect;
        vpX = 0.f;
        vpY = (winH - vpH) * 0.5f;
    }

    VkViewport vp = {};
    vp.x = vpX;
    vp.y = vpY + vpH;
    vp.width = vpW;
    vp.height = -vpH;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.offset = {(int32_t)vpX, (int32_t)vpY};
    scissor.extent = {(uint32_t)vpW, (uint32_t)vpH};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_vertexBuf && m_indexBuf && m_indexCount > 0)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_descSets[m_currentImageIndex], 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuf, &offset);
        vkCmdBindIndexBuffer(cmd, m_indexBuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
    }

    if (m_stencilVertBuf && m_stencilIdxBuf && !m_stencilDraws.empty())
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_descSets[m_currentImageIndex], 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_stencilVertBuf, &offset);
        vkCmdBindIndexBuffer(cmd, m_stencilIdxBuf, 0, VK_INDEX_TYPE_UINT32);

        VkMemoryBarrier stencilBarrier = {};
        stencilBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        stencilBarrier.srcAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        stencilBarrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        for (auto &sd : m_stencilDraws)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              sd.evenOdd ? m_stencilEOPipeline
                                         : m_stencilNZPipeline);
            vkCmdDrawIndexed(cmd, sd.fanIndexCount, 1, sd.fanFirstIndex,
                             sd.vertexOffset, 0);

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 VK_DEPENDENCY_BY_REGION_BIT, 1,
                                 &stencilBarrier, 0, nullptr, 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_stencilReadPipeline);
            vkCmdDrawIndexed(cmd, 6, 1, sd.bboxFirstIndex, sd.vertexOffset, 0);
        }
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_queryPools[m_currentFrame], 1);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m_imageAvailSem[m_currentFrame];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_renderDoneSem[m_currentImageIndex];

    vkResetFences(m_device, 1, &m_frameFence[m_currentFrame]);
    VK_CHECK(
        vkQueueSubmit(m_graphicsQueue, 1, &si, m_frameFence[m_currentFrame]),
        "vkQueueSubmit");

    uint64_t timestamps[2] = {0, 0};
    vkGetQueryPoolResults(m_device, m_queryPools[m_currentFrame], 0, 2,
                          sizeof(timestamps), timestamps, sizeof(uint64_t),
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    float gpuMs = (timestamps[1] - timestamps[0]) * m_timestampPeriod * 1e-6f;

    auto cpuEnd = std::chrono::high_resolution_clock::now();
    float cpuMs =
        std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
    float fps = 1000.0f / cpuMs;
    printf("[PERF] CPU: %6.2f ms GPU: %6.2f ms FPS: %6.1f\r", cpuMs, gpuMs,
           fps);
}

void VulkanSVGRenderer::present(bool vsync)
{
    VkPresentModeKHR desired =
        vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_renderDoneSem[m_currentImageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &pi);

    bool outOfDate = (result == VK_ERROR_OUT_OF_DATE_KHR ||
                      result == VK_SUBOPTIMAL_KHR || m_needsResize);
    bool vsyncChanged = (desired != m_presentMode);

    if (outOfDate || vsyncChanged)
    {
        RLOG("present: %s%s - recreating swapchain",
             outOfDate ? "out-of-date/suboptimal " : "",
             vsyncChanged ? "vsync-changed" : "");
        m_presentMode = desired;
        recreateSwapchain();
        m_needsResize = false;
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

bool VulkanSVGRenderer::saveFrameToPPM(const std::string &path)
{
    vkDeviceWaitIdle(m_device);

    const uint32_t width = m_swapExtent.width;
    const uint32_t height = m_swapExtent.height;
    VkImage srcImage = m_swapImages[m_currentImageIndex];

    bool bgrOrder = (m_swapFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                     m_swapFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                     m_swapFormat == VK_FORMAT_B8G8R8A8_SNORM);

    VkDeviceSize bufSize = VkDeviceSize(width) * VkDeviceSize(height) * 4;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!createBuffer(bufSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuf, stagingMem))
    {
        return false;
    }

    VkCommandBufferAllocateInfo cbai = {};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandPool = m_cmdPool;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cbai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier toTransferSrc = {};
    toTransferSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransferSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransferSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferSrc.image = srcImage;
    toTransferSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransferSrc.srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    toTransferSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransferSrc);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuf, 1, &region);

    VkImageMemoryBarrier backToPresent = toTransferSrc;
    backToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    backToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &backToPresent);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);

    bool ok = false;
    void *mapped = nullptr;
    if (vkMapMemory(m_device, stagingMem, 0, bufSize, 0, &mapped) == VK_SUCCESS)
    {
        const uint8_t *src = reinterpret_cast<const uint8_t *>(mapped);

        std::ofstream out(path, std::ios::binary);
        if (out)
        {
            out << "P6\n" << width << ' ' << height << "\n255\n";

            std::vector<uint8_t> row(size_t(width) * 3);
            for (uint32_t y = 0; y < height && out; y++)
            {
                const uint8_t *srcRow = src + VkDeviceSize(y) * width * 4;
                for (uint32_t x = 0; x < width; x++)
                {
                    const uint8_t *px = srcRow + size_t(x) * 4;
                    if (bgrOrder)
                    {
                        row[size_t(x) * 3 + 0] = px[2]; // R
                        row[size_t(x) * 3 + 1] = px[1]; // G
                        row[size_t(x) * 3 + 2] = px[0]; // B
                    }
                    else
                    {
                        row[size_t(x) * 3 + 0] = px[0]; // R
                        row[size_t(x) * 3 + 1] = px[1]; // G
                        row[size_t(x) * 3 + 2] = px[2]; // B
                    }
                }
                out.write(reinterpret_cast<const char *>(row.data()),
                          std::streamsize(row.size()));
            }
            ok = bool(out);
            out.close();
        }
        vkUnmapMemory(m_device, stagingMem);
    }

    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    RLOG("\n[RNDR] saveFrameToPPM: %s -> %s (%ux%u)", path.c_str(),
         ok ? "ok" : "FAILED", width, height);
    return ok;
}