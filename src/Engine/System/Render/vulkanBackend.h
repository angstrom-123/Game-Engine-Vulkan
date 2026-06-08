#pragma once

/* Descriptor Sets 
 *  
 *  Set 0:
 *      0 - Per Frame Uniform Buffer        - uniform buffer
 *      1 - Light Buffer                    - storage buffer
 *      2 - Shadow Buffer                   - storage buffer
 *
 *  Set 1:
 *      0 - Texture Array Sampler           - combined image sampler array
 *      1 - Shadow Array Comparison Sampler - combined image sampler array
 *      2 - Shadow Array Sampler            - combined image sampler array
 *      3 - HDR Sampler                     - combined image sampler
 *      4 - LDR Sampler                     - combined image sampler
 *      5 - Bloom Sampler                   - combined image sampler
 *
 *  Set 2:
 *      0 - Light Indices                   - storage buffer
 *      1 - Light Tile Counts               - storage buffer
 *      2 - GBuffer Array                   - combined image sampler array
 *      3 - HDR Image                       - storage image
 *
 *  Set 3:
 *      0 - Bloom Ping-Pong Buffer          - storage image
 *      1 - Bloom Downsample Pyramid        - storage image array
 *
 *                    Descriptor Set   Pipeline Layout 
 *                     0   1   2   3  
 *  Depth Pre        | x | x |   |   |     Minimal     |
 *  GBuffer          | x | x |   |   |     Minimal     |
 *  Shadowmap        | x | x |   |   |     Shadow      |
 *  Light Culling    | x |   | x |   |     Culling     |
 *  Lighting         | x | x | x |   |     Lighting    |
 *  Transparency     | x | x | x |   |     Lighting    |
 *  Light Extraction | x |   | x | x |      Bloom      |
 *  Downsampling     | x |   | x | x |      Bloom      |
 *  Blurring         | x |   | x | x |      Bloom      |
 *  Upsampling       | x |   | x | x |      Bloom      |
 *  Tone Mapping     | x | x |   |   |     Minimal     |
 *  Anti Aliasing    | x | x |   |   |     Minimal     |
 */

#include "ECS/ecs.h"
#include "ECS/ecsTypes.h"
#include "Geometry/vertex.h"
#include "ResourceManager/imageResource.h"
#include "device.h"
#include "System/Render/texture.h"
#include "pipeline.h"
#include "backendTypes.h"
#include "commandSubmitter.h"
#include "config.h"
#include "mipGenerator.h"
#include <vulkan/vulkan_core.h>

class VulkanBackend {
public:
    ~VulkanBackend();
    VulkanBackend() = default;
    void Init(struct GLFWwindow *window, const Config &config);
    void InitFrontend(GraphicsFrontend& frontend, const GraphicsFrontendInfo& info);
    void CleanupFrontend(GraphicsFrontend& frontend);
    void Draw(ECS *ecs, GraphicsFrontend& frontend, const std::set<Entity>& entities);
    AllocatedBuffer AllocateVertexBuffer(const Vertex *const vertices, uint32_t count);
    AllocatedTexture AllocateTexture(ImageResource& image, GraphicsFrontend& frontend);
    AllocatedTexture AllocateTexture(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, GraphicsFrontend& frontend);
    uint32_t AllocateShadowMap(GraphicsFrontend& frontend);
    void RequestResize(const GraphicsFrontend& frontend) { m_ResizeCameras.push_back(frontend.camera); }

public:
    // A funny idiom, publicly viewable but not editable and don't need for a getter
    const uint64_t& frameNumber{m_InternalFrameNumber};

    Device device;
    VulkanBackendSettings settings;

    CommandSubmitter submitter;
    FrameData frames[FRAMES_IN_FLIGHT];

    void ReadConfig(const Config& config);
    void Resize(ECS *ecs);
    void InitVulkan(struct GLFWwindow *window);
    void InitDynamics();
    void InitSamplers();
    void InitDescriptors();
    void UpdateDescriptors();
    void InitBuffers();
    void InitPipelines();

#ifdef PROFILING
    void InitProfiling();
#endif

private:
    uint64_t m_InternalFrameNumber{0};
    std::vector<std::pair<float, Entity>> m_SortingBuffer;

    // TODO Move like everything into device
    PFN_vkCmdBeginRenderingKHR pfn_CmdBeginRendering{nullptr};
    PFN_vkCmdEndRenderingKHR pfn_CmdEndRendering{nullptr};

    VkPresentModeKHR m_RequestedPresentMode{VK_PRESENT_MODE_IMMEDIATE_KHR};

    VkExtent2D m_Extent{0};
    VkViewport m_Viewport{0};
    VkRect2D m_ScissorRect{0};

    VkExtent2D m_ShadowExtent{0};
    VkViewport m_ShadowViewport{0};
    VkRect2D m_ShadowScissorRect{0};

    std::deque<std::function<void ()>> m_DynamicDeleter;
    std::deque<std::function<void ()>> m_MainDeleter;
    std::deque<Entity> m_ResizeCameras;

    // These structures are really large so I allocate them on the heap
    Texture *m_TexturePool;
    Texture *m_DepthTarget;
    Texture *m_AlbedoTarget;
    Texture *m_NormalTarget;
    Texture *m_MaterialTarget;
    Texture *m_LightingTarget;
    Texture *m_BloomPingPongTarget;
    uint32_t m_BloomPyramidMipCount{0};
    Texture *m_BloomPyramidTargets[MAX_BLOOM_MIPS];
    Texture *m_ToneMapTarget;

    VkSampler m_OffscreenSampler{VK_NULL_HANDLE};
    VkSampler m_TextureSampler{VK_NULL_HANDLE};
    VkSampler m_NormalSampler{VK_NULL_HANDLE};
    VkSampler m_ComparisonShadowSampler{VK_NULL_HANDLE};
    VkSampler m_LinearShadowSampler{VK_NULL_HANDLE};

    // Graphics
    VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DummyDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout0{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout1{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout2{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout3{VK_NULL_HANDLE};

    Pipeline m_DepthPipeline;
    Pipeline m_ShadowPipeline;
    Pipeline m_GBufferPipeline;
    Pipeline m_LightCullingPipeline;
    Pipeline m_LightingPipeline;
    Pipeline m_BloomLightExtractionPipeline;
    Pipeline m_BloomDownsamplePipeline;
    Pipeline m_BloomHorizontalBlurPipeline;
    Pipeline m_BloomVerticalBlurPipeline;
    Pipeline m_BloomAccumulatePipeline;
    Pipeline m_TransparencyPipeline;
    Pipeline m_ToneMapPipeline;
    Pipeline m_AntiAliasingPipeline;

    MipGenerator m_NormalMipGenerator;
};
