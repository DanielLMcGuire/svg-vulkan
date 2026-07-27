#include "renderer.h"
#include "renderer_internal.h"
#include <cstring>
#include <fstream>

static std::vector<char> readFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

bool VulkanSVGRenderer::createRenderPass()
{
    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);

    VkAttachmentDescription dsAttach = {};
    dsAttach.format = m_dsFormat;
    dsAttach.samples = m_msaaSamples;
    dsAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    dsAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dsAttach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    dsAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dsAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dsAttach.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDependency selfDep = {};
    selfDep.srcSubpass = 0;
    selfDep.dstSubpass = 0;
    selfDep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    selfDep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    selfDep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    selfDep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    selfDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    if (!msaa)
    {
        RLOG("RenderPass: single-sample (colour + DS)");
        VkAttachmentDescription attachments[2] = {};

        attachments[0].format = m_swapFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1] = dsAttach;

        VkAttachmentReference colRef = {
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference dsRef = {
            1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub = {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colRef;
        sub.pDepthStencilAttachment = &dsRef;

        VkSubpassDependency extDep = {};
        extDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        extDep.dstSubpass = 0;
        extDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        extDep.srcAccessMask = 0;
        extDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        extDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency deps[] = {extDep, selfDep};

        VkRenderPassCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2;
        ci.pAttachments = attachments;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 2;
        ci.pDependencies = deps;
        VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass),
                 "vkCreateRenderPass");
        return true;
    }

    RLOG("RenderPass: MSAA x%d (MS colour + resolve + DS)", (int)m_msaaSamples);
    VkAttachmentDescription attachments[3] = {};

    attachments[0].format = m_swapFormat;
    attachments[0].samples = m_msaaSamples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = m_swapFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[2] = dsAttach;

    VkAttachmentReference colorRef = {0,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolveRef = {
        1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dsRef = {
        2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pResolveAttachments = &resolveRef;
    sub.pDepthStencilAttachment = &dsRef;

    VkSubpassDependency extDep = {};
    extDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    extDep.dstSubpass = 0;
    extDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    extDep.srcAccessMask = 0;
    extDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    extDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency deps[] = {extDep, selfDep};

    VkRenderPassCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 3;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VK_CHECK(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass),
             "vkCreateRenderPass MSAA");
    return true;
}

bool VulkanSVGRenderer::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &uboBinding;
    VK_CHECK(
        vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_descSetLayout),
        "vkCreateDescriptorSetLayout");
    return true;
}

VkShaderModule
VulkanSVGRenderer::createShaderModule(const std::vector<char> &code)
{
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(m_device, &ci, nullptr, &mod),
             "vkCreateShaderModule");
    return mod;
}

void VulkanSVGRenderer::initShaders()
{
    RLOG("Loading shaders...");
    auto vertCode = readFile("shader.vert.spv");
    RLOG("Loaded shader.vert.spv");
    auto fragCode = readFile("shader.frag.spv");
    RLOG("Loaded shader.frag.spv");

    RLOG("Initializing shaders...");
    m_vertShaderModule = createShaderModule(vertCode);
    RLOG("Initialized vertex shader");
    m_fragShaderModule = createShaderModule(fragCode);
    RLOG("Initialized fragment shader");
}

bool VulkanSVGRenderer::createPipeline()
{
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertShaderModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_fragShaderModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(Mesh::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(Mesh::Vertex, x);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = offsetof(Mesh::Vertex, r);

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                  VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineViewportStateCreateInfo vps = {};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = m_msaaSamples;

    VkPipelineColorBlendAttachmentState blendOn = {};
    blendOn.blendEnable = VK_TRUE;
    blendOn.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendOn.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendOn.colorBlendOp = VK_BLEND_OP_ADD;
    blendOn.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendOn.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendOn.alphaBlendOp = VK_BLEND_OP_ADD;
    blendOn.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState blendOff = blendOn;
    blendOff.blendEnable = VK_FALSE;
    blendOff.colorWriteMask = 0;

    VkPipelineColorBlendStateCreateInfo blendStateOn = {};
    blendStateOn.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendStateOn.attachmentCount = 1;
    blendStateOn.pAttachments = &blendOn;

    VkPipelineColorBlendStateCreateInfo blendStateOff = blendStateOn;
    blendStateOff.pAttachments = &blendOff;

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_descSetLayout;
    VK_CHECK(
        vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout),
        "vkCreatePipelineLayout");

    VkPipelineDepthStencilStateCreateInfo dsOff = {};
    dsOff.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkStencilOpState nzFront = {};
    nzFront.failOp = VK_STENCIL_OP_KEEP;
    nzFront.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    nzFront.depthFailOp = VK_STENCIL_OP_KEEP;
    nzFront.compareOp = VK_COMPARE_OP_ALWAYS;
    nzFront.writeMask = 0xFF;
    nzFront.compareMask = 0xFF;
    nzFront.reference = 0;

    VkStencilOpState nzBack = nzFront;
    nzBack.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;

    VkPipelineDepthStencilStateCreateInfo dsNZ = {};
    dsNZ.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsNZ.stencilTestEnable = VK_TRUE;
    dsNZ.front = nzFront;
    dsNZ.back = nzBack;

    VkStencilOpState eoState = {};
    eoState.failOp = VK_STENCIL_OP_KEEP;
    eoState.passOp = VK_STENCIL_OP_INVERT;
    eoState.depthFailOp = VK_STENCIL_OP_KEEP;
    eoState.compareOp = VK_COMPARE_OP_ALWAYS;
    eoState.writeMask = 0xFF;
    eoState.compareMask = 0xFF;
    eoState.reference = 0;

    VkPipelineDepthStencilStateCreateInfo dsEO = {};
    dsEO.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsEO.stencilTestEnable = VK_TRUE;
    dsEO.front = eoState;
    dsEO.back = eoState;

    VkStencilOpState readState = {};
    readState.failOp = VK_STENCIL_OP_KEEP;
    readState.passOp = VK_STENCIL_OP_REPLACE;
    readState.depthFailOp = VK_STENCIL_OP_KEEP;
    readState.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    readState.writeMask = 0xFF;
    readState.compareMask = 0xFF;
    readState.reference = 0;

    VkPipelineDepthStencilStateCreateInfo dsRead = {};
    dsRead.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsRead.stencilTestEnable = VK_TRUE;
    dsRead.front = readState;
    dsRead.back = readState;

    VkGraphicsPipelineCreateInfo base = {};
    base.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    base.stageCount = 2;
    base.pStages = stages;
    base.pVertexInputState = &vi;
    base.pInputAssemblyState = &ia;
    base.pViewportState = &vps;
    base.pRasterizationState = &rs;
    base.pMultisampleState = &ms;
    base.pDynamicState = &dyn;
    base.layout = m_pipelineLayout;
    base.renderPass = m_renderPass;

    VkGraphicsPipelineCreateInfo infos[4] = {base, base, base, base};
    infos[0].pDepthStencilState = &dsOff;
    infos[0].pColorBlendState = &blendStateOn;
    infos[1].pDepthStencilState = &dsNZ;
    infos[1].pColorBlendState = &blendStateOff;
    infos[2].pDepthStencilState = &dsEO;
    infos[2].pColorBlendState = &blendStateOff;
    infos[3].pDepthStencilState = &dsRead;
    infos[3].pColorBlendState = &blendStateOn;

    VkPipeline pipes[4] = {};
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 4, infos,
                                       nullptr, pipes),
             "vkCreateGraphicsPipelines");

    m_pipeline = pipes[0];
    m_stencilNZPipeline = pipes[1];
    m_stencilEOPipeline = pipes[2];
    m_stencilReadPipeline = pipes[3];
    RLOG("Pipelines created: main + stencilNZ + stencilEO + stencilRead");
    return true;
}

bool VulkanSVGRenderer::createFramebuffers()
{
    bool msaa = (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT);
    m_framebuffers.resize(m_swapImageViews.size());
    RLOG("Framebuffers: count=%d  msaa=%d", (int)m_swapImageViews.size(),
         (int)msaa);
    for (size_t i = 0; i < m_swapImageViews.size(); i++)
    {
        VkImageView fbAttachments[3];
        uint32_t attachCount;
        if (msaa)
        {
            fbAttachments[0] = m_msaaView;
            fbAttachments[1] = m_swapImageViews[i];
            fbAttachments[2] = m_dsView;
            attachCount = 3;
        }
        else
        {
            fbAttachments[0] = m_swapImageViews[i];
            fbAttachments[1] = m_dsView;
            attachCount = 2;
        }

        VkFramebufferCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = m_renderPass;
        ci.attachmentCount = attachCount;
        ci.pAttachments = fbAttachments;
        ci.width = m_swapExtent.width;
        ci.height = m_swapExtent.height;
        ci.layers = 1;
        VK_CHECK(
            vkCreateFramebuffer(m_device, &ci, nullptr, &m_framebuffers[i]),
            "vkCreateFramebuffer");
    }
    return true;
}
