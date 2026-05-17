#pragma once

// Pipeline:
//      - GBuffer Pass 
//          - Albedo r8g8b8
//          - Normal r10g10b10a2
//          - Material r8g8b8a8
//          - Depth d32
//      - SSAO Pass (1/2 resolution, upscale blur)
//      - Light Culling Compute 
//      - Shading r16g16b16
//      - Bloom
//          - downscale upscale 
//      - Transparent Forward 
//          - Sorted back to front 
//      - Post process
//          - Tone map, color grade, anti alias, present
//      - UI / Text
//          - ortho cam, unlit
#include "ECS/ecs.h"
#include "ECS/ecsTypes.h"
#include "Geometry/vertex.h"
#include "backendV2Types.h"
#include "System/Render/commandSubmitter.h"
#include "config.h"
#include "vulkan_core.h"

#ifdef DEBUG 
    #define USE_VALIDATION_LAYERS VK_TRUE 
    #define VK_CHECK(x)\
        do {\
            VkResult __err = x;\
            if (__err) {\
                ERROR("Vulkan error: " << (VkResult) __err);\
                abort();\
            }\
        } while (0);
#elifdef RELEASE
    #define USE_VALIDATION_LAYERS VK_FALSE 
    #define VK_CHECK(x) (void) x
#else 
    #error "DEBUG or RELEASE must be specified"
#endif

namespace fs = std::filesystem;

class VulkanBackendV2 {
public:
    ~VulkanBackendV2();
    void Init(struct GLFWwindow *window, Config &config);
    void InitFrontend(GraphicsFrontend& frontend, const GraphicsFrontendInfo& info);
    void CleanupFrontend(GraphicsFrontend& frontend);
    void Draw(ECS *ecs, GraphicsFrontend& frontend, const std::set<Entity>& entities);
    AllocatedBuffer AllocateVertexBuffer(const Vertex *const vertices, uint32_t count);
    AllocatedTexture AllocateTexture(ImageResource& image, GraphicsFrontend& frontend);
    AllocatedTexture AllocateTexture(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, GraphicsFrontend& frontend);
    uint32_t AllocateShadowcaster(GraphicsFrontend& frontend);
    void RequestResize(const GraphicsFrontend& frontend) { m_ResizeCameras.push_back(frontend.camera); }
    void WaitForIdle() { VK_CHECK(vkDeviceWaitIdle(device)); }
    
public:
    // A bit of a funny idiom, viewable externally but not editable without getter - controller by m_InternalFrameNumber
    const uint64_t& frameNumber{m_InternalFrameNumber};

    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};

    VkQueue graphicsQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamily{0};

    struct VmaAllocator_T *allocator{nullptr};
    CommandSubmitter submitter;
    FrameDataV2 frames[FRAMES_IN_FLIGHT];

private:
    enum PipelineDepthFlag {
        PIPELINE_DEPTH_NONE = 0x0,
        PIPELINE_DEPTH_WRITE = 0x1,
        PIPELINE_DEPTH_TEST = 0x2,
        PIPELINE_DEPTH_BIAS = 0x4,
    };
    struct GraphicsPipelineCreateInfo {
        std::string pipelineName{"unnamed pipeline"};
        fs::path vertexShader{""};
        fs::path fragmentShader{""};
        VkViewport *viewport{nullptr};
        VkRect2D *scissor{nullptr};
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VertexInputDesc *vertexInput{nullptr};
        VkCullModeFlags cullMode{VK_CULL_MODE_NONE};
        bool blendEnable{false};
        uint32_t depthFlags{PIPELINE_DEPTH_NONE};
        VkCompareOp depthCompare{VK_COMPARE_OP_LESS_OR_EQUAL};
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        std::initializer_list<VkFormat> attachmentFormats{};
    };

    void Resize(ECS *ecs);
    void InitVulkan(struct GLFWwindow *window);
    void InitDynamics();
    void InitSamplers();
    void InitDescriptors();
    void UpdateDescriptors();
    void InitBuffers();
    void InitPipelines();
    VkPipeline CreateGraphicsPipeline(const GraphicsPipelineCreateInfo&& info);
    VkShaderModule LoadShaderModule(const fs::path& path);
    template<typename T> T GetFunctionPointer(const char *name) 
    { 
        T result = reinterpret_cast<T>(vkGetInstanceProcAddr(m_Instance, name)); 
        ASSERT(result && "Failed to get function pointer");
        return result;
    }
#ifdef PROFILING
    void InitProfiling();
#endif

private:
    uint64_t m_InternalFrameNumber{0};
    std::vector<std::pair<float, Entity>> m_SortingBuffer;

    VkInstance m_Instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_DebugMessenger{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties m_PhysicalDeviceProperties{0};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR m_PFNGetSurfaceCapabilities{nullptr};
    PFN_vkCmdBeginRenderingKHR m_PFNCmdBeginRendering{nullptr};
    PFN_vkCmdEndRenderingKHR m_PFNCmdEndRendering{nullptr};

    VkSurfaceKHR m_Surface{VK_NULL_HANDLE};
    VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};
    VkFormat m_SwapchainFormat{VK_FORMAT_UNDEFINED};
    VkPresentModeKHR m_PresentMode{VK_PRESENT_MODE_IMMEDIATE_KHR};
    std::vector<SwapchainImageDataV2> m_Images;

    VkExtent2D m_Extent{100, 100};
    VkViewport m_Viewport{0.0, 0.0, 100.0, 100.0, 0.0, 1.0};
    VkRect2D m_ScissorRect{{0, 0}, {100, 100}};

    VkExtent2D m_ShadowExtent{SHADOW_RESOLUTION, SHADOW_RESOLUTION};
    VkViewport m_ShadowViewport{0.0, 0.0, SHADOW_RESOLUTION, SHADOW_RESOLUTION, 0.0, 1.0};
    VkRect2D m_ShadowScissorRect{{0, 0}, {SHADOW_RESOLUTION, SHADOW_RESOLUTION}};

    std::deque<std::function<void ()>> m_DynamicDeleter;
    std::deque<std::function<void ()>> m_MainDeleter;
    std::deque<Entity> m_ResizeCameras;

    OffscreenTarget m_DepthTarget;
    OffscreenTarget m_AlbedoTarget;
    OffscreenTarget m_NormalTarget;
    OffscreenTarget m_MaterialTarget;
    OffscreenTarget m_LightingTarget;
    OffscreenTarget m_ToneMapTarget;

    VkSampler m_LinearSampler{VK_NULL_HANDLE};
    VkSampler m_NormalSampler{VK_NULL_HANDLE};
    VkSampler m_ComparisonSampler{VK_NULL_HANDLE};

    VkCommandPool m_CommandPool{VK_NULL_HANDLE};

    VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DummyDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout0{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout1{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout2{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout3{VK_NULL_HANDLE};

    VkPipelineLayout m_DepthPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_ShadowPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_GBufferPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_TransparencyPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_LightingPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_ToneMapPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_AntiAliasingPipelineLayout{VK_NULL_HANDLE};

    VkPipeline m_DepthPipeline{VK_NULL_HANDLE};
    VkPipeline m_ShadowPipeline{VK_NULL_HANDLE};
    VkPipeline m_GBufferPipeline{VK_NULL_HANDLE};
    VkPipeline m_LightingPipeline{VK_NULL_HANDLE};
    VkPipeline m_TransparencyPipeline = VK_NULL_HANDLE;
    VkPipeline m_ToneMapPipeline{VK_NULL_HANDLE};
    VkPipeline m_AntiAliasingPipeline{VK_NULL_HANDLE};
};
