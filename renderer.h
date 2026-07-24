#pragma once
#ifdef _WIN32
#  define VK_USE_PLATFORM_WIN32_KHR
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#  define NOMINMAX
#  endif
#  include <windows.h>
#else
#  define VK_USE_PLATFORM_XLIB_KHR
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/Xos.h>
#endif
#pragma comment(lib, "vulkan-1.lib")
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "tessellator.h"



static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

class VulkanSVGRenderer {
public:
    VulkanSVGRenderer()  = default;
    ~VulkanSVGRenderer();
#ifdef _WIN32
    bool init(HWND hwnd, int width, int height);
#else
    bool init(Display* display, Window window, int width, int height);
#endif
    void loadMesh(const Mesh& mesh);
    void loadDocument(const SVGDocument& doc);
    void loadSVGString(const std::string& svg);
    void resize(int width, int height);
    void render(float bgR = 1.f, float bgG = 1.f, float bgB = 1.f);
    void present(bool vsync = true);
    bool saveFrameToPPM(const std::string& path);

    VkDevice         device()   const { return m_device; }
    VkPhysicalDevice physDev()  const { return m_physDevice; }
    VkInstance       instance() const { return m_instance; }
    const std::string& svgTitle() const { return m_svgTitle; }

private:
    bool createInstance();
#ifdef _WIN32
    bool createSurface(HWND hwnd);
#else
    bool createSurface(Display* display, Window window);
#endif
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createUniformBuffers();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createCommandBuffers();
    bool createSyncObjects();

    void cleanupSwapchain();
    void recreateSwapchain();

    VkSampleCountFlagBits getMaxSampleCount();
    bool createMSAAResources();
    void cleanupMSAAResources();

    VkFormat findDepthStencilFormat();
    bool     createDepthStencilResources();
    void     cleanupDepthStencilResources();

    bool uploadVertexBuffer(const std::vector<Mesh::Vertex>& verts);
    bool uploadIndexBuffer(const std::vector<uint32_t>& indices);
    bool uploadStencilBuffers(const std::vector<Mesh::Vertex>& verts,
                              const std::vector<uint32_t>& indices);
    void destroyGeometryBuffers();

    void updateUBO(uint32_t imageIndex, float bgR, float bgG, float bgB);

    uint32_t      findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
    bool          createBuffer(VkDeviceSize size,
                               VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props,
                               VkBuffer& outBuf, VkDeviceMemory& outMem);
    void          copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    VkShaderModule m_vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule m_fragShaderModule = VK_NULL_HANDLE;

    void initShaders();

    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physDevice     = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  m_presentQueue   = VK_NULL_HANDLE;
    uint32_t                 m_graphicsFamily = UINT32_MAX;
    uint32_t                 m_presentFamily  = UINT32_MAX;

#ifdef _DEBUG
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
#endif

    VkSwapchainKHR              m_swapchain      = VK_NULL_HANDLE;
    std::vector<VkImage>        m_swapImages;
    std::vector<VkImageView>    m_swapImageViews;
    VkFormat                    m_swapFormat     = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D                  m_swapExtent     = {};
    VkPresentModeKHR            m_presentMode    = VK_PRESENT_MODE_FIFO_KHR;

    VkRenderPass             m_renderPass          = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_descSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout         m_pipelineLayout      = VK_NULL_HANDLE;

    VkPipeline               m_pipeline            = VK_NULL_HANDLE;
    VkPipeline               m_stencilNZPipeline   = VK_NULL_HANDLE;
    VkPipeline               m_stencilEOPipeline   = VK_NULL_HANDLE;

    VkPipeline               m_stencilReadPipeline = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_framebuffers;

    VkCommandPool                m_cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmdBuffers;

    VkSemaphore m_imageAvailSem[MAX_FRAMES_IN_FLIGHT] = {};
    VkFence     m_frameFence   [MAX_FRAMES_IN_FLIGHT] = {};
    std::vector<VkSemaphore> m_renderDoneSem;
    std::vector<VkFence>     m_imageInFlight;
    uint32_t    m_currentFrame      = 0;
    uint32_t    m_currentImageIndex = 0;

    VkBuffer       m_vertexBuf  = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMem  = VK_NULL_HANDLE;
    VkBuffer       m_indexBuf   = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMem   = VK_NULL_HANDLE;
    uint32_t       m_indexCount = 0;

    VkBuffer       m_stencilVertBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_stencilVertMem = VK_NULL_HANDLE;
    VkBuffer       m_stencilIdxBuf  = VK_NULL_HANDLE;
    VkDeviceMemory m_stencilIdxMem  = VK_NULL_HANDLE;

    struct StencilFillDraw {
        uint32_t fanFirstIndex;
        uint32_t fanIndexCount;
        uint32_t bboxFirstIndex;
        int32_t  vertexOffset;
        bool     evenOdd;
    };
    std::vector<StencilFillDraw> m_stencilDraws;

    std::vector<VkBuffer>       m_uboBufs;
    std::vector<VkDeviceMemory> m_uboMems;
    std::vector<void*>          m_uboMapped;

    VkDescriptorPool             m_descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descSets;

    VkQueryPool m_queryPools[MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
    float m_timestampPeriod = 1.0f;

    int   m_width  = 0;
    int   m_height = 0;
    float m_svgW   = 800.f;
    float m_svgH   = 600.f;
    bool  m_needsResize = false;
    std::string m_svgTitle;

    VkSampleCountFlagBits m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkImage               m_msaaImage   = VK_NULL_HANDLE;
    VkDeviceMemory        m_msaaMem     = VK_NULL_HANDLE;
    VkImageView           m_msaaView    = VK_NULL_HANDLE;

    VkFormat       m_dsFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    VkImage        m_dsImage  = VK_NULL_HANDLE;
    VkDeviceMemory m_dsMem    = VK_NULL_HANDLE;
    VkImageView    m_dsView   = VK_NULL_HANDLE;
};
