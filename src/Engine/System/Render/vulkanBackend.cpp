#include "vulkanBackend.h"
#include "System/Render/backendTypes.h"
#include "System/Render/texture.h"
#include "Util/enumIndex.h"
#include "config.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>

#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#define VMA_IMPLEMENTATION
#ifdef DEBUG
    #define VMA_LEAK_LOG_FORMAT(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
    #define VMA_ASSERT_LEAK(expr) if (!(expr)) { abort(); }
#endif
#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#include "backendDefs.h"
#include "Component/shadowcaster.h"
#include "ECS/ecsTypes.h"

#include "Component/camera.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"

#include "Geometry/frustum.h"
#include "Geometry/vertex.h"

#include "Util/allocator.h"
#include "Util/profiler.h"

#include "handle.h"

#ifdef PROFILING 
    #include "Util/profiler.h"

    const uint32_t HEADING_COUNT = 10;
    
    static uint32_t s_GpuTimestampIndex = 0;
    #define GPU_PROFILING_BEGIN_FRAME() s_GpuTimestampIndex = 0
    #define GPU_PROFILING_TIMESTAMP(cmd) \
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, s_GpuTimestampIndex++)
    #define GPU_PROFILING_END_FRAME(frame) \
        vkGetQueryPoolResults(device, frame.queryPool, 0, frame.queryTimestamps.size(), frame.queryTimestamps.size() * sizeof(uint64_t), frame.queryTimestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);\
        float periodNS = m_PhysicalDeviceProperties.limits.timestampPeriod;\
        float times[HEADING_COUNT] = {0.0};\
        uint32_t timestampIndex = 0;\
        for (uint32_t i = 0; i < HEADING_COUNT - 1; i++) {\
            times[i] = (frame.queryTimestamps[timestampIndex + 1] - frame.queryTimestamps[timestampIndex]) * periodNS;\
            timestampIndex += 2;\
        }\
        times[HEADING_COUNT - 1] = (frame.queryTimestamps[timestampIndex - 1] - frame.queryTimestamps[0]) * periodNS;\
        PROFILER_GPU_VALUES(times);
        
#else    
    #define GPU_PROFILING_BEGIN_FRAME()
    #define GPU_PROFILING_TIMESTAMP(cmd)
    #define GPU_PROFILING_END_FRAME(frame)
#endif

VulkanBackend::~VulkanBackend()
{
    device.WaitForIdle();

    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) (*it)();
    m_DynamicDeleter.clear();

    for (auto it = m_MainDeleter.rbegin(); it != m_MainDeleter.rend(); it++) (*it)();
    m_MainDeleter.clear();

}

void VulkanBackend::ReadConfig(const Config& config)
{
    m_RequestedPresentMode = config.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    m_Extent = { config.windowWidth, config.windowHeight };
    
    settings.vsync = config.vsync;
    settings.bloomEnabled = config.bloom;
    switch (config.shadowQuality) {
        case ShadowQuality::LOW:
            settings.shadowResolution = 512;
            break;
        case ShadowQuality::MEDIUM:
            settings.shadowResolution = 1024;
            break;
        case ShadowQuality::HIGH:
            settings.shadowResolution = 2048;
            break;
        case ShadowQuality::ULTRA:
            settings.shadowResolution = 4096;
            break;
        default:
            UNREACHABLE("Bad shadow quality");
    };

    m_ShadowExtent = { settings.shadowResolution, settings.shadowResolution };
    m_ShadowViewport = {
        .width = static_cast<float>(m_ShadowExtent.width),
        .height = static_cast<float>(m_ShadowExtent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };
    m_ShadowScissorRect = { 
        .offset = {0, 0}, 
        .extent = m_ShadowExtent 
    };
}

void VulkanBackend::Init(struct GLFWwindow *window, const Config &config)
{
    ReadConfig(config);

    m_SortingBuffer.reserve(MAX_ENTITIES);

    m_Viewport = {
        .width = static_cast<float>(m_Extent.width),
        .height = static_cast<float>(m_Extent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };
    m_ScissorRect = { 
        .extent = m_Extent 
    };

    // Manually allocate all render target texture memory up front to avoid reallocation
    m_TexturePool = static_cast<Texture *>(::operator new(sizeof(Texture) * (7 + MAX_BLOOM_MIPS)));
    m_MainDeleter.push_back([this] {
        ::operator delete(m_TexturePool);
    });

    InitVulkan(window);
    InitDynamics();
    InitSamplers();
    InitBuffers();
    InitDescriptors();
    InitPipelines();

    m_NormalMipGenerator = MipGenerator(device, MAX_NORMAL_MIPS);
    m_MainDeleter.push_back([this] {
        m_NormalMipGenerator.Cleanup(device);
    });

#ifdef PROFILING 
    InitProfiling();
#endif
}

void VulkanBackend::InitFrontend(GraphicsFrontend& frontend, const GraphicsFrontendInfo& info)
{
    // ================================================== Params ==================================================

    ASSERT(info.camera != INVALID_HANDLE && "Invalid camera");
    frontend.camera = info.camera;
    m_ResizeCameras.push_back(frontend.camera);

    // ================================================== Texture Arrays ==================================================
    const uint32_t MIN_ARRAY_LAYERS = 8;

    frontend.arrayTextures[EnumBase(TextureArrayID::COLOR_SMALL)] = Texture(device, settings.colorTextureFormat, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::COLOR_SMALL)]), 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::COLOR_SMALL)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::MIP_MAPS));
    frontend.arrayTextures[EnumBase(TextureArrayID::COLOR_LARGE)] = Texture(device, settings.colorTextureFormat, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::COLOR_LARGE)]), 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::COLOR_LARGE)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::MIP_MAPS));
    frontend.arrayTextures[EnumBase(TextureArrayID::DATA_SMALL)] = Texture(device, settings.dataTextureFormat, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
            glm::uvec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::DATA_SMALL)]), 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::DATA_SMALL)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::NON_COLOR) | EnumBase(TextureFlag::MIP_MAPS));
    frontend.arrayTextures[EnumBase(TextureArrayID::DATA_LARGE)] = Texture(device, settings.dataTextureFormat, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
            glm::uvec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::DATA_LARGE)]), 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::DATA_LARGE)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::NON_COLOR) | EnumBase(TextureFlag::MIP_MAPS));
    frontend.arrayTextures[EnumBase(TextureArrayID::FONT)] = Texture(device, settings.fontTextureFormat, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::FONT)]), 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::FONT)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::MIP_MAPS));

    // Transition all to shader readable
    submitter.ImmediateSubmit(device, [&](VkCommandBuffer commandBuffer) {
        frontend.arrayTextures[EnumBase(TextureArrayID::COLOR_SMALL)].Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frontend.arrayTextures[EnumBase(TextureArrayID::COLOR_LARGE)].Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frontend.arrayTextures[EnumBase(TextureArrayID::DATA_SMALL)].Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frontend.arrayTextures[EnumBase(TextureArrayID::DATA_LARGE)].Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frontend.arrayTextures[EnumBase(TextureArrayID::FONT)].Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    VkDescriptorImageInfo arrayImageInfos[EnumBase(TextureArrayID::MAX_ENUM)] = {};
    for (uint32_t arrayID = 0; arrayID < EnumBase(TextureArrayID::MAX_ENUM); arrayID++) {
        arrayImageInfos[arrayID] = {
            .sampler = m_TextureSampler,
            .imageView = frontend.arrayTextures[arrayID].GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    };

    VkWriteDescriptorSet writes[FRAMES_IN_FLIGHT] = {};
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet1,
            .dstBinding = 0,
            .descriptorCount = static_cast<uint32_t>(TextureArrayID::MAX_ENUM),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = arrayImageInfos
        };
    }
    vkUpdateDescriptorSets(device.GetDevice(), FRAMES_IN_FLIGHT, writes, 0, nullptr);

    // ================================================== Shadow Arrays ==================================================

    frontend.maxShadows = glm::min(info.maxShadows, settings.maxShadowcasters);
    frontend.shadowArrayTexture = Texture(device, settings.depthFormat, 
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
            glm::uvec2(settings.shadowResolution), 
            VK_IMAGE_ASPECT_DEPTH_BIT, 
            glm::max(info.arrayLayers[EnumBase(TextureArrayID::FONT)], MIN_ARRAY_LAYERS),
            EnumBase(TextureFlag::LAYER_VIEWS));
    submitter.ImmediateSubmit(device, [&](VkCommandBuffer commandBuffer) {
        frontend.shadowArrayTexture.Transition(commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    VkDescriptorImageInfo shadowImageInfo = {
        .sampler = m_ComparisonShadowSampler,
        .imageView = frontend.shadowArrayTexture.GetView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    // Lighting descriptor set
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet1,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &shadowImageInfo
        };
    }
    vkUpdateDescriptorSets(device.GetDevice(), FRAMES_IN_FLIGHT, writes, 0, nullptr);
}

void VulkanBackend::CleanupFrontend(GraphicsFrontend& frontend)
{
    frontend.camera = INVALID_HANDLE;

    for (Texture& texture : frontend.arrayTextures) {
        texture.Cleanup(device);
    }

    frontend.shadowArrayTexture.Cleanup(device);
}

void VulkanBackend::Draw(ECS *ecs, GraphicsFrontend& frontend, const std::set<Entity>& entities)
{
    GPU_PROFILING_BEGIN_FRAME();

    // ================================================== Handle Resize Requests ==================================================

    if (!m_ResizeCameras.empty()) {
        Resize(ecs);
    }

    // ================================================== Acquire Swapchain Image ==================================================

    size_t frameIndex = m_InternalFrameNumber % FRAMES_IN_FLIGHT;
    FrameData &frame = frames[frameIndex];

    device.WaitForFence(&frame.renderFence);
    device.ResetFence(&frame.renderFence);

    std::optional<SwapchainImage> acquireResult = device.AcquireSwapchainImage(frame.acquireSemaphore);
    if (!acquireResult.has_value()) {
        RequestResize(frontend);
        return;
    }
    SwapchainImage swapchainImage = acquireResult.value();

    if (swapchainImage.fence != VK_NULL_HANDLE && swapchainImage.fence != frame.renderFence) {
        device.WaitForFence(&swapchainImage.fence);
    }
    device.SetSwapchainFence(swapchainImage, frame.renderFence);

    Texture swapchainTarget(device, swapchainImage.image, swapchainImage.view, swapchainImage.format,
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    // ================================================== Update Per-Frame Uniforms ==================================================

    // Set up frustum culling
    Camera& camera = ecs->GetComponent<Camera>(frontend.camera);
    glm::vec4 cameraPos = glm::vec4(ecs->GetComponent<Transform>(frontend.camera).GlobalTranslation(ecs), 1.0);

    // Copy across light data
    const auto[lightCount, lights] = ecs->GetData<Light>();
    std::memset(frame.lightBuffer.data, 0, MAX_LIGHTS * sizeof(Light));
    std::memcpy(frame.lightBuffer.data, lights, lightCount * sizeof(Light));

    // Copy across shadow data 
    const auto[shadowCount, shadowcasters] = ecs->GetData<Shadowcaster>();
    std::memset(frame.shadowBuffer.data, 0, settings.maxShadowcasters * sizeof(Shadowcaster));
    std::memcpy(frame.shadowBuffer.data, shadowcasters, shadowCount * sizeof(Shadowcaster));

     // Zero light culling outputs
    uint32_t tilesX = (m_Extent.width + settings.computeTileSize - 1) / settings.computeTileSize;
    uint32_t tilesY = (m_Extent.height + settings.computeTileSize  - 1) / settings.computeTileSize;
    vkCmdFillBuffer(frame.commandBuffer, frame.lightIndexBuffer.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(frame.commandBuffer, frame.lightTileCountBuffer.buffer, 0, VK_WHOLE_SIZE, 0);
    VkBufferMemoryBarrier fillBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = frame.lightIndexBuffer.buffer,
        .size = VK_WHOLE_SIZE
    };
    VkBufferMemoryBarrier fillBarriers[2] = { fillBarrier, fillBarrier };
    fillBarriers[1].buffer = frame.lightTileCountBuffer.buffer;
    vkCmdPipelineBarrier(frame.commandBuffer, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
            0, 0, nullptr, 2, fillBarriers, 0, nullptr);

    // Update uniform buffer
    PerFrameUniforms *uniforms = static_cast<PerFrameUniforms *>(frame.uniformBuffer.data);
    uniforms->camera = {
        .view = camera.view,
        .proj = camera.projection,
        .invView = glm::inverse(camera.view),
        .invProj = glm::inverse(camera.projection),
        .position = cameraPos,
        .near = camera.near,
        .far = camera.far,
    };
    uniforms->settings = {
        .exposure = 1.0,
        .gamma = 2.2,
        .ambientIntensity = 0.15,
        .lightCount = lightCount,
        .screenSize = glm::ivec2(m_Extent.width, m_Extent.height),
        .shadowSize = glm::ivec2(settings.shadowResolution),
    };
    uniforms->lightCulling = {
        .tileSize = settings.computeTileSize,
        .tilesX = tilesX,
        .tilesY = tilesY,
        .maxLightsPerTile = settings.maxLightsPerTile
    };

    // ================================================== Organize Entities ==================================================

    Frustum cameraFrustum = Frustum(camera.projection * camera.view);
    auto PredicateIsVisible = [ecs, &cameraFrustum](Entity e) {
        glm::mat4x4 model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs);
        CentreExtents centreExtents = CentreExtents(ecs->GetComponent<Mesh>(e).bounds);
        return cameraFrustum.Intersects(centreExtents.ToWorldSpace(model));
    };
    auto PredicateIsTransparent = [ecs](Entity e) {
        return ecs->GetComponent<Material>(e).flags & EnumBase(MaterialFlag::TRANSPARENT);
    };

    // Frustum Culled
    auto visibleEntities = entities | std::views::filter(PredicateIsVisible);

    // Culled + Opaque
    auto visibleOpaqueEntities = visibleEntities | std::views::filter(std::not_fn(PredicateIsTransparent));

    // Culled + Sorted + Transparent
    m_SortingBuffer.resize(0);
    for (Entity e : visibleEntities) {
        // Skip opaque
        if (!(ecs->GetComponent<Material>(e).flags & EnumBase(MaterialFlag::TRANSPARENT))) {
            continue;
        }

        const Transform& transform = ecs->GetComponent<Transform>(e);
        glm::vec3 meshCentre = ecs->GetComponent<Mesh>(e).bounds.Centroid();

        glm::vec3 worldCentre = transform.GlobalRotation(ecs) 
                * (meshCentre * transform.GlobalScale(ecs)) 
                + transform.GlobalTranslation(ecs);
        glm::vec3 difference = worldCentre - glm::vec3(cameraPos);
        float distanceSquared = glm::dot(difference, difference);
        m_SortingBuffer.emplace_back(distanceSquared, e);
    }
    std::sort(m_SortingBuffer.begin(), m_SortingBuffer.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    const auto& visibleTransparentEntities = m_SortingBuffer;

    { // ================================================== Depth Pre-Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition depth image to depth attachment
        m_DepthTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        // Begin rendering
        VkDescriptorSet descriptorSets[2] = {
            frame.descriptorSet0,
            frame.descriptorSet1
        };
        DescriptorSets descriptors = { descriptorSets, 2 };
        m_DepthPipeline.BeginRendering(frame.commandBuffer, pfn_CmdBeginRendering, descriptors);
        {
            for (const Entity e : visibleOpaqueEntities) {
                VertexPushConstants vertexConstants = {
                    .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_DepthPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 
                        0, sizeof(VertexPushConstants), 
                        &vertexConstants);

                FragmentPushConstants fragmentConstants = { 
                    .material = ecs->GetComponent<Material>(e).Pack(),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_DepthPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 
                        sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                        &fragmentConstants);

                const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                VkDeviceSize offsets = 0;
                vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
            }
        }
        m_DepthPipeline.EndRendering(frame.commandBuffer, pfn_CmdEndRendering);

        // Transition depth attachment to read only
        m_DepthTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Depth Pre-Pass ==================================================

    { // ================================================== GBuffer Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition color images to attachment
        m_AlbedoTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_NormalTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_MaterialTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Begin Rendering
        VkDescriptorSet descriptorSets[2] = {
            frame.descriptorSet0,
            frame.descriptorSet1
        };
        DescriptorSets descriptors = { descriptorSets, 2 };
        m_GBufferPipeline.BeginRendering(frame.commandBuffer, pfn_CmdBeginRendering, descriptors);
        {
            for (const Entity e : visibleOpaqueEntities) {
                VertexPushConstants vertexConstants = {
                    .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_GBufferPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 
                        0, sizeof(VertexPushConstants), 
                        &vertexConstants);

                FragmentPushConstants fragmentConstants = { 
                    .material = ecs->GetComponent<Material>(e).Pack(),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_GBufferPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 
                        sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                        &fragmentConstants);

                const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                VkDeviceSize offsets = 0;
                vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
            }
        }
        m_GBufferPipeline.EndRendering(frame.commandBuffer, pfn_CmdEndRendering);

        // Transition color attachments to shader readable 
        m_AlbedoTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_NormalTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_MaterialTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) GBuffer Pass ==================================================

    { // ================================================== Shadowcaster Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        if (frontend.maxShadows > 0) {
            // Transition shadow array to attachment
            frontend.shadowArrayTexture.Transition(frame.commandBuffer, 
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

            auto opaqueEntities = entities | std::views::filter(std::not_fn(PredicateIsTransparent));

            VkDescriptorSet descriptorSets[2] = {
                frame.descriptorSet0,
                frame.descriptorSet1
            };
            DescriptorSets descriptors = { descriptorSets, 2 };
            m_ShadowPipeline.PrepareForDynamicRendering(frame.commandBuffer, descriptors);

            for (uint32_t i = 0; i < shadowCount; i++) {
                const Shadowcaster& shadowcaster = shadowcasters[i];

                for (uint32_t j = 0; j < SHADOW_CASCADE_COUNT; j++) {
                    const Shadowcaster::Cascade& cascade = shadowcaster.cascades[j];
                    if (cascade.far < 0.0) {
                        break;
                    }

                    m_ShadowPipeline.BeginDynamicDepthRendering(frame.commandBuffer, 
                            pfn_CmdBeginRendering, 
                            frontend.shadowArrayTexture.GetLayerView(cascade.shadowMapIndex),
                            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
                    {
                        Frustum cascadeFrustum = Frustum(cascade.vp);
                        for (const Entity e : opaqueEntities) {
                            const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                            const glm::mat4x4& model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs);

                            // Frustum culling
                            CentreExtents centreExtents = CentreExtents(mesh.bounds);
                            if (!cascadeFrustum.Intersects(centreExtents.ToWorldSpace(model))) {
                                continue;
                            }
                            ShadowPushConstants constants = {
                                .model = model,
                                .vp = cascade.vp,
                            };
                            vkCmdPushConstants(frame.commandBuffer, 
                                    m_ShadowPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 
                                    0, sizeof(ShadowPushConstants), 
                                    &constants);

                            VkDeviceSize offsets = 0;
                            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                            vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
                        }
                    }
                    m_ShadowPipeline.EndDynamicRendering(frame.commandBuffer, pfn_CmdEndRendering);
                }
            }

            // Transition shadow array to shader readable
            frontend.shadowArrayTexture.Transition(frame.commandBuffer, 
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Shadowcaster Pass ==================================================

    { // ================================================== Light Culling Compute Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        VkDescriptorSet descriptorSets[3] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet,
            frame.descriptorSet2
        };
        DescriptorSets descriptors = { descriptorSets, 3 };
        m_LightCullingPipeline.BeginCompute(frame.commandBuffer, descriptors);
        {
            vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);
        }
        m_LightCullingPipeline.EndCompute();

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Light Culling Compute Pass ==================================================

    { // ================================================== Lighting Compute Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition HDR image to general
        m_LightingTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_GENERAL);

        VkDescriptorSet descriptorSets[3] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1, 
            frame.descriptorSet2 
        };
        DescriptorSets descriptors = { descriptorSets, 3 };
        m_LightingPipeline.BeginCompute(frame.commandBuffer, descriptors);
        {
            vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);
        }
        m_LightingPipeline.EndCompute();

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Lighting Compute Pass ==================================================

    { // ================================================== Transparency Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        if (!visibleTransparentEntities.empty()) {
            m_LightingTarget->Transition(frame.commandBuffer, 
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            // // Begin Rendering
            VkDescriptorSet descriptorSets[3] = { 
                frame.descriptorSet0, 
                frame.descriptorSet1, 
                frame.descriptorSet2 
            };
            DescriptorSets descriptors = { descriptorSets, 3 };
            m_TransparencyPipeline.BeginRendering(frame.commandBuffer, pfn_CmdBeginRendering, descriptors);
            {
                for (const auto& [_, e] : visibleTransparentEntities) {
                    VertexPushConstants vertexConstants = {
                        .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
                    };
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_TransparencyPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 
                            0, sizeof(VertexPushConstants), 
                            &vertexConstants);

                    FragmentPushConstants fragmentConstants = { 
                        .material = ecs->GetComponent<Material>(e).Pack(),
                    };
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_TransparencyPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 
                            sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                            &fragmentConstants);

                    const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                    VkDeviceSize offsets = 0;
                    vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                    vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
                }
            }
            m_TransparencyPipeline.EndRendering(frame.commandBuffer, pfn_CmdEndRendering);

            // Transition hdr image from attachment to general
            m_LightingTarget->Transition(frame.commandBuffer, 
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_GENERAL);
        }

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Transparency ==================================================

    { // ================================================== Bloom Compute ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        if (settings.bloomEnabled) {
            VkMemoryBarrier writeBarrier = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
            };

            // Transition ping-pong image to general
            m_BloomPingPongTarget->Transition(frame.commandBuffer, 
                    0, VK_ACCESS_SHADER_WRITE_BIT, 
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_GENERAL);

            // Transition pyramid to general
            for (uint32_t i = 0; i < m_BloomPyramidMipCount; i++) {
                m_BloomPyramidTargets[i]->Transition(frame.commandBuffer, 
                        0, VK_ACCESS_SHADER_WRITE_BIT, 
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_IMAGE_LAYOUT_GENERAL);
            }

            // Downsample hdr image into pyramid[0]
            VkDescriptorSet descriptorSets[4] = { 
                frame.descriptorSet0, 
                frame.dummyDescriptorSet, 
                frame.descriptorSet2, 
                frame.descriptorSet3, 
            };
            DescriptorSets descriptors = { descriptorSets, 4 };
            m_BloomDownsamplePipeline.BeginCompute(frame.commandBuffer, descriptors);
            {
                BloomPushConstants constants = {
                    .currentIndex = 0,
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_BloomDownsamplePipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                        0, sizeof(BloomPushConstants), 
                        &constants);
                uint32_t w = glm::max(m_Extent.width >> 1, 1u);
                uint32_t h = glm::max(m_Extent.height >> 1, 1u);
                uint32_t groupsX = (w + settings.computeTileSize - 1) / settings.computeTileSize;
                uint32_t groupsY = (h + settings.computeTileSize - 1) / settings.computeTileSize;
                vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);
            }
            m_BloomDownsamplePipeline.EndCompute();

            // Wait first downsample to complete
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

            // In place light extraction on pyramid[0]
            m_BloomLightExtractionPipeline.BeginCompute(frame.commandBuffer, descriptors);
            {
                vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);
            }
            m_BloomLightExtractionPipeline.EndCompute();

            // Wait for bright pass to complete
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

            // Downsample Pyramid

            // Dispatch Compute for each layer
            m_BloomDownsamplePipeline.BeginCompute(frame.commandBuffer, descriptors);
            {
                for (uint32_t i = 1; i < m_BloomPyramidMipCount; i++) {
                    BloomPushConstants constants = {
                        .currentIndex = i,
                    };
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_BloomDownsamplePipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                            0, sizeof(BloomPushConstants), 
                            &constants);

                    uint32_t w = glm::max(m_Extent.width >> (i + 1), 1u);
                    uint32_t h = glm::max(m_Extent.height >> (i + 1), 1u);
                    uint32_t groupsX = (w + settings.computeTileSize - 1) / settings.computeTileSize;
                    uint32_t groupsY = (h + settings.computeTileSize - 1) / settings.computeTileSize;
                    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

                    // Wait for current layer to downsample before dispatching next
                    vkCmdPipelineBarrier(frame.commandBuffer, 
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                            0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
                }
            }
            m_BloomDownsamplePipeline.EndCompute();

            // Blur 

            // Dispatch Compute for each layer
            for (uint32_t i = 0; i < m_BloomPyramidMipCount; i++) {
                const uint32_t BLUR_COMPUTE_GROUP_SIZE = 256;
                uint32_t w = glm::max(m_Extent.width >> (i + 1), 1u);
                uint32_t h = glm::max(m_Extent.height >> (i + 1), 1u);

                uint32_t groupsX = (w + BLUR_COMPUTE_GROUP_SIZE - 1) / BLUR_COMPUTE_GROUP_SIZE;
                uint32_t groupsY = h;

                BloomPushConstants constants = {
                    .currentIndex = i,
                    .blurKernelRadius = BLOOM_BLUR_RADIUS,
                    .blurKernelWeights = { 0.007, 0.032, 0.059, 0.079, 0.088, 0.079, 0.059, 0.032, 0.007 }, // Gaussian
                };

                m_BloomHorizontalBlurPipeline.BeginCompute(frame.commandBuffer, descriptors);
                {
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_BloomHorizontalBlurPipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                            0, sizeof(BloomPushConstants), 
                            &constants);
                    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);
                }
                m_BloomHorizontalBlurPipeline.EndCompute();

                // Wait for horizontal blurs to finish before the vertical ones are dispatched
                vkCmdPipelineBarrier(frame.commandBuffer, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

                groupsX = w;
                groupsY = (h + BLUR_COMPUTE_GROUP_SIZE - 1) / BLUR_COMPUTE_GROUP_SIZE;

                m_BloomVerticalBlurPipeline.BeginCompute(frame.commandBuffer, descriptors);
                {
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_BloomVerticalBlurPipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                            0, sizeof(BloomPushConstants), 
                            &constants);
                    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);
                }
                m_BloomVerticalBlurPipeline.EndCompute();

                // Wait for vertical blurs to finish before next level or upsampling
                vkCmdPipelineBarrier(frame.commandBuffer, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
            }

            // Upsample / Accumulate

            m_BloomAccumulatePipeline.BeginCompute(frame.commandBuffer, descriptors);
            {
                // NOTE: Using int32_t instead of uint32_t for i because the decrement causes underflow when i = 0
                for (int32_t i = m_BloomPyramidMipCount - 2; i >= 0; i--) { // Shader upsamples the layer above (smaller)
                    BloomPushConstants constants = {
                        .currentIndex = static_cast<uint32_t>(i),
                        .accumulationFactor = 0.1,
                    };
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_BloomAccumulatePipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
                            0, sizeof(BloomPushConstants), 
                            &constants);

                    const uint32_t UPSAMPLE_COMPUTE_GROUP_SIZE = 8;
                    uint32_t w = glm::max(m_Extent.width >> (i + 1), 1u);
                    uint32_t h = glm::max(m_Extent.height >> (i + 1), 1u);

                    uint32_t groupsX = (w + UPSAMPLE_COMPUTE_GROUP_SIZE - 1) / UPSAMPLE_COMPUTE_GROUP_SIZE;
                    uint32_t groupsY = h;
                    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

                    // Wait for current layer to accumulate before starting next
                    vkCmdPipelineBarrier(frame.commandBuffer, 
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                            0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
                }
            }
            m_BloomAccumulatePipeline.EndCompute();

            // Transition highest pyramid level (where the accumulated bloom is) to shader readable
            m_BloomPyramidTargets[0]->Transition(frame.commandBuffer, 
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else {
            // Transition highest pyramid level to shader readable
            m_BloomPyramidTargets[0]->Transition(frame.commandBuffer, 
                    0, VK_ACCESS_SHADER_READ_BIT, 
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Bloom Compute ==================================================

    { // ================================================== Tone-Mapping Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition target to color attachment
        m_ToneMapTarget->Transition(frame.commandBuffer, 
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Transition HDR image to shader readonly
        m_LightingTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Begin rendering
        VkDescriptorSet descriptorSets[2] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1, 
        };
        DescriptorSets descriptors = { descriptorSets, 2 };
        m_ToneMapPipeline.BeginRendering(frame.commandBuffer, pfn_CmdBeginRendering, descriptors);
        {
            ToneMapPushConstants constants = {
                .bloomEnabled = (settings.bloomEnabled) ? 1u : 0u
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_ToneMapPipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 
                    0, sizeof(ToneMapPushConstants), &constants);
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        }
        m_ToneMapPipeline.EndRendering(frame.commandBuffer, pfn_CmdEndRendering);

        // Transition tone mapped attachment to shader readable 
        m_ToneMapTarget->Transition(frame.commandBuffer, 
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Tone-Mapping Pass ==================================================

    { // ================================================== (SWAPCHAIN) Anti-Aliasing Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition swapchain image to attachment attachment
        swapchainTarget.Transition(frame.commandBuffer, 
                0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // // Begin rendering
        VkDescriptorSet descriptorSets[2] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1, 
        };
        DescriptorSets descriptors = { descriptorSets, 2 };
        m_AntiAliasingPipeline.PrepareForDynamicRendering(frame.commandBuffer, descriptors);
        m_AntiAliasingPipeline.BeginDynamicColorRendering(frame.commandBuffer, pfn_CmdBeginRendering, 
                swapchainTarget.GetView(), 
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
        {
            vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        }
        m_AntiAliasingPipeline.EndDynamicRendering(frame.commandBuffer, pfn_CmdEndRendering);

        // Transition swapchain attachment to present source
        swapchainTarget.Transition(frame.commandBuffer, 
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) (SWAPCHAIN) Anti-Aliasing Pass ==================================================

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    GPU_PROFILING_END_FRAME(frame);

    // ================================================== Submit and Present ==================================================

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.acquireSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &swapchainImage.semaphore
    };
    VK_CHECK(vkQueueSubmit(device.GetGraphicsQueue(), 1, &submitInfo, frame.renderFence));

    VkSwapchainKHR swapchain = device.GetSwapchain();
    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &swapchainImage.semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &swapchainImage.index,
    };
    VkResult presentErr = vkQueuePresentKHR(device.GetGraphicsQueue(), &presentInfo);
    if (presentErr == VK_ERROR_OUT_OF_DATE_KHR || presentErr == VK_SUBOPTIMAL_KHR) {
        RequestResize(frontend);
        return;
    } else {
        VK_CHECK(presentErr);
    }

    m_InternalFrameNumber++;
}

AllocatedBuffer VulkanBackend::AllocateVertexBuffer(const Vertex *const vertices, uint32_t count)
{
    AllocatedBuffer result;
    AllocatedBuffer stagingBuffer;

    // Create staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = count * sizeof(Vertex),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };
    vmaCreateBuffer(device.GetAllocator(), &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);

    vmaMapMemory(device.GetAllocator(), stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, vertices, count * sizeof(Vertex));
    vmaUnmapMemory(device.GetAllocator(), stagingBuffer.allocation);

    // Create vertex buffer
    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = count * sizeof(Vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateBuffer(device.GetAllocator(), &vertexBufferInfo, &allocInfo, &result.buffer, &result.allocation, nullptr);

    // Copy the buffer to the GPU
    submitter.ImmediateSubmit(device, [&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = count * sizeof(Vertex),
        };
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, result.buffer, 1, &copy);
    });

    // Clean up staging buffer
    vmaDestroyBuffer(device.GetAllocator(), stagingBuffer.buffer, stagingBuffer.allocation);

    return result;
}

AllocatedTexture VulkanBackend::AllocateTexture(ImageResource& image, GraphicsFrontend& frontend)
{
    // Select the right array
    auto Fits = [this](glm::ivec2 imageSize, TextureArrayID arrayID) {
        int32_t resolution = settings.textureArrayResolutions[EnumBase(arrayID)];
        return imageSize.x <= resolution && imageSize.y <= resolution;
    };

    TextureArrayID arrayID = TextureArrayID::MAX_ENUM;
    bool isTransparent = image.flags & EnumBase(ImageFlag::TRANSPARENT);
    bool isFontAtlas = image.flags & EnumBase(ImageFlag::FONT_ATLAS);
    bool isNonColor = image.flags & EnumBase(ImageFlag::NON_COLOR);
    bool isCutout = image.flags & EnumBase(ImageFlag::CUTOUT);

    if (isFontAtlas && image.size == glm::ivec2(settings.textureArrayResolutions[EnumBase(TextureArrayID::FONT)])) {
        arrayID = TextureArrayID::FONT;
    } else if (isNonColor) {
        if (Fits(image.size, TextureArrayID::DATA_SMALL)) {
            arrayID = TextureArrayID::DATA_SMALL;
        } else if (Fits(image.size, TextureArrayID::DATA_LARGE)) {
            arrayID = TextureArrayID::DATA_LARGE;
        }
    } else if (isTransparent || isCutout || image.flags == 0) {
        if (Fits(image.size, TextureArrayID::COLOR_SMALL)) {
            arrayID = TextureArrayID::COLOR_SMALL;
        } else if (Fits(image.size, TextureArrayID::COLOR_LARGE)) {
            arrayID = TextureArrayID::COLOR_LARGE;
        }
    }
    if (arrayID == TextureArrayID::MAX_ENUM) {
        FATAL("Texture too large for arrays (" << image.size.x << ", " << image.size.y << ")");
    }

    // Allocate
    Texture& arrayTexture = frontend.arrayTextures[EnumBase(arrayID)];
    uint32_t layerIndex = arrayTexture.AllocateLayer();
    if (isNonColor) {
        arrayTexture.CopyToLayer(device, submitter, m_NormalMipGenerator, image, layerIndex);
    } else {
        arrayTexture.CopyToLayer(device, submitter, {}, image, layerIndex);
    }
    AllocatedTexture allocation = { arrayID, layerIndex };

    return allocation;
}

AllocatedTexture VulkanBackend::AllocateTexture(
        uint8_t *pixels,
        glm::ivec2 size,
        ImageFlags flags,
        int32_t channels,
        GraphicsFrontend& frontend)
{
    ImageResource tmp{flags, size, pixels, channels};
    return AllocateTexture(tmp, frontend);
}

void VulkanBackend::Resize(ECS *ecs)
{
    PROFILER_PROFILE_SCOPE("VulkanBackend::Resize");

    INFO("Resized");

    device.WaitForIdle();

    for (FrameData& frame : frames) {
        VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    }

    // Cleanup old resources
    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) (*it)();
    m_DynamicDeleter.clear();

    // Recreate resources
    InitDynamics();
    UpdateDescriptors();

    // Update static pipeline attachments
    m_DepthPipeline.UpdateDepthAttachment(*m_DepthTarget);
    m_GBufferPipeline.UpdateDepthAttachment(*m_DepthTarget);
    m_GBufferPipeline.UpdateColorAttachment(0, *m_AlbedoTarget);
    m_GBufferPipeline.UpdateColorAttachment(1, *m_NormalTarget);
    m_GBufferPipeline.UpdateColorAttachment(2, *m_MaterialTarget);
    m_TransparencyPipeline.UpdateDepthAttachment(*m_DepthTarget);
    m_TransparencyPipeline.UpdateColorAttachment(0, *m_LightingTarget);
    m_ToneMapPipeline.UpdateColorAttachment(0, *m_ToneMapTarget);

    // Update graphics pipeline extents
    m_DepthPipeline.UpdateBounds(m_Viewport, m_ScissorRect, m_Extent);
    m_GBufferPipeline.UpdateBounds(m_Viewport, m_ScissorRect, m_Extent);
    m_TransparencyPipeline.UpdateBounds(m_Viewport, m_ScissorRect, m_Extent);
    m_ToneMapPipeline.UpdateBounds(m_Viewport, m_ScissorRect, m_Extent);
    m_AntiAliasingPipeline.UpdateBounds(m_Viewport, m_ScissorRect, m_Extent);

    // Resize cameras
    for (const Entity e : m_ResizeCameras) {
        Camera& cam = ecs->GetComponent<Camera>(e);
        if (cam.perspective) {
            float aspect = static_cast<float>(m_Extent.width) / static_cast<float>(m_Extent.height);
            cam.projection = Camera::VulkanPerspective(cam.fov, aspect, cam.near, cam.far);
        } else {
            glm::vec2 dimensions = (glm::vec2(m_Extent.width, m_Extent.height) * cam.scale) / 2.0f;
            cam.projection = Camera::VulkanOrthographic(-dimensions.x, dimensions.x, -dimensions.y, dimensions.y, cam.near, cam.far);
        }
    }
    m_ResizeCameras.clear();
}

uint32_t VulkanBackend::AllocateShadowMap(GraphicsFrontend& frontend)
{
    return frontend.shadowArrayTexture.AllocateLayer();
}

void VulkanBackend::InitVulkan(GLFWwindow *window)
{
    device = Device(window, m_RequestedPresentMode, USE_VALIDATION_LAYERS);
    device.EnqueueCleanup(m_MainDeleter);

    pfn_CmdBeginRendering = device.GET_FUNCTION_POINTER(vkCmdBeginRenderingKHR);
    pfn_CmdEndRendering = device.GET_FUNCTION_POINTER(vkCmdEndRenderingKHR);

    for (FrameData& frame : frames) {
        frame.commandBuffer = device.AllocateCommandBuffer();
    }

    submitter.Init(device);
    m_MainDeleter.push_back([=, this] {
        submitter.Cleanup(device);
    });
}

void VulkanBackend::InitDynamics()
{
    // ================================================== Swapchain ==================================================

    if (device.GetSwapchain() == VK_NULL_HANDLE) {
        m_Extent = device.CreateSwapchain(FRAMES_IN_FLIGHT + 1);
    } else {
        m_Extent = device.RecreateSwapchain();
    }

    m_Viewport.width = m_Extent.width;
    m_Viewport.height = m_Extent.height;
    m_ScissorRect.extent = m_Extent;

    // ================================================== Render Targets ==================================================

    // NOTE: This placement new stuff relies on Texture having a trivial destructor. This is statically asserted in the class.

    uint32_t poolIndex = 0;

    m_DepthTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.depthFormat, 
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_DEPTH_BIT, 1, 0);
    m_DepthTarget->EnqueueCleanup(device, m_DynamicDeleter);

    m_AlbedoTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.albedoFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_AlbedoTarget->EnqueueCleanup(device, m_DynamicDeleter);

    m_NormalTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.normalFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_NormalTarget->EnqueueCleanup(device, m_DynamicDeleter);

    m_MaterialTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.materialFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_MaterialTarget->EnqueueCleanup(device, m_DynamicDeleter);

    m_LightingTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.lightingFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_LightingTarget->EnqueueCleanup(device, m_DynamicDeleter);
    
    uint32_t mipCount = static_cast<uint32_t>(glm::floor(std::log2(glm::max(m_Extent.width, m_Extent.height))));
    m_BloomPyramidMipCount = glm::min(mipCount, MAX_BLOOM_MIPS);
    for (uint32_t i = 0; i < m_BloomPyramidMipCount; i++) {
        auto resolution = glm::uvec2(glm::max(m_Extent.width >> (i + 1), 1u), glm::max(m_Extent.height >> (i + 1), 1u));
        m_BloomPyramidTargets[i] = new (&m_TexturePool[poolIndex++]) Texture(device, settings.bloomFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
            resolution, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
        m_BloomPyramidTargets[i]->EnqueueCleanup(device, m_DynamicDeleter);
    }

    m_BloomPingPongTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.bloomFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, 
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_BloomPingPongTarget->EnqueueCleanup(device, m_DynamicDeleter);

    m_ToneMapTarget = new (&m_TexturePool[poolIndex++]) Texture(device, settings.tonemapFormat, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            glm::uvec2(m_Extent.width, m_Extent.height), VK_IMAGE_ASPECT_COLOR_BIT, 1, 0);
    m_ToneMapTarget->EnqueueCleanup(device, m_DynamicDeleter);

    // ================================================== Sync Structs ==================================================

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (FrameData& frame : frames) {
        VK_CHECK(vkCreateFence(device.GetDevice(), &fenceInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(device.GetDevice(), &semaphoreInfo, nullptr, &frame.acquireSemaphore));

        m_DynamicDeleter.push_back([=, this] {
            vkDestroyFence(device.GetDevice(), frame.renderFence, nullptr);
            vkDestroySemaphore(device.GetDevice(), frame.acquireSemaphore, nullptr);
        });
    }
}

void VulkanBackend::InitBuffers()
{
    // ================================================== Per Frame Buffers ==================================================

    VkBufferCreateInfo uniformBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(PerFrameUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
    };

    VkBufferCreateInfo lightBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = MAX_LIGHTS * sizeof(Light),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
    };

    VkBufferCreateInfo shadowBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = settings.maxShadowcasters * sizeof(Shadowcaster),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
    };

    uint32_t tilesX = (m_Extent.width + settings.computeTileSize - 1) / settings.computeTileSize;
    uint32_t tilesY = (m_Extent.height + settings.computeTileSize - 1) / settings.computeTileSize;
    VkBufferCreateInfo lightIndexBufferInfo = { // Up to MAX_LIGHTS_PER_TILE indices per tile
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = settings.maxLightsPerTile * tilesX * tilesY * sizeof(uint32_t),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
    };
    VkBufferCreateInfo lightTileCountBufferInfo = { // One count for each tile
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = tilesX * tilesY * sizeof(uint32_t),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
    };

    VmaAllocationCreateInfo gpuBufferAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };

    VmaAllocationCreateInfo cpuGpuBufferAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU
    };

    for (FrameData& frame : frames) {
        vmaCreateBuffer(device.GetAllocator(), 
                &uniformBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr);
        vmaMapMemory(device.GetAllocator(), frame.uniformBuffer.allocation, &frame.uniformBuffer.data);

        vmaCreateBuffer(device.GetAllocator(), 
                &lightBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.lightBuffer.buffer, &frame.lightBuffer.allocation, nullptr);
        vmaMapMemory(device.GetAllocator(), frame.lightBuffer.allocation, &frame.lightBuffer.data);

        vmaCreateBuffer(device.GetAllocator(), 
                &shadowBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.shadowBuffer.buffer, &frame.shadowBuffer.allocation, nullptr);
        vmaMapMemory(device.GetAllocator(), frame.shadowBuffer.allocation, &frame.shadowBuffer.data);

        vmaCreateBuffer(device.GetAllocator(), 
                &lightIndexBufferInfo, &gpuBufferAllocInfo, 
                &frame.lightIndexBuffer.buffer, &frame.lightIndexBuffer.allocation, nullptr);
        vmaMapMemory(device.GetAllocator(), frame.lightIndexBuffer.allocation, &frame.lightIndexBuffer.data);

        vmaCreateBuffer(device.GetAllocator(), 
                &lightTileCountBufferInfo, &gpuBufferAllocInfo, 
                &frame.lightTileCountBuffer.buffer, &frame.lightTileCountBuffer.allocation, nullptr);
        vmaMapMemory(device.GetAllocator(), frame.lightTileCountBuffer.allocation, &frame.lightTileCountBuffer.data);

        m_MainDeleter.push_back([frame, this] {
            vmaUnmapMemory(device.GetAllocator(), frame.uniformBuffer.allocation);
            vmaDestroyBuffer(device.GetAllocator(), frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);

            vmaUnmapMemory(device.GetAllocator(), frame.lightBuffer.allocation);
            vmaDestroyBuffer(device.GetAllocator(), frame.lightBuffer.buffer, frame.lightBuffer.allocation);

            vmaUnmapMemory(device.GetAllocator(), frame.shadowBuffer.allocation);
            vmaDestroyBuffer(device.GetAllocator(), frame.shadowBuffer.buffer, frame.shadowBuffer.allocation);

            vmaUnmapMemory(device.GetAllocator(), frame.lightIndexBuffer.allocation);
            vmaDestroyBuffer(device.GetAllocator(), frame.lightIndexBuffer.buffer, frame.lightIndexBuffer.allocation);

            vmaUnmapMemory(device.GetAllocator(), frame.lightTileCountBuffer.allocation);
            vmaDestroyBuffer(device.GetAllocator(), frame.lightTileCountBuffer.buffer, frame.lightTileCountBuffer.allocation);
        });
    }
}

void VulkanBackend::InitSamplers()
{
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 4.0,
        .minLod = 0.0,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE
    };
    VK_CHECK(vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &m_TextureSampler));

    // Distinct sampler for normal maps in case I need to adjust anisotropy or mipmapping
    VK_CHECK(vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &m_NormalSampler));

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &m_OffscreenSampler));

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_GREATER;
    samplerInfo.maxLod = 1.0;
    samplerInfo.mipLodBias = 0.0;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 0.0;
    VK_CHECK(vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &m_ComparisonShadowSampler));
    VK_CHECK(vkCreateSampler(device.GetDevice(), &samplerInfo, nullptr, &m_LinearShadowSampler));

    m_MainDeleter.push_back([=, this] {
        vkDestroySampler(device.GetDevice(), m_OffscreenSampler, nullptr);
        vkDestroySampler(device.GetDevice(), m_TextureSampler, nullptr);
        vkDestroySampler(device.GetDevice(), m_NormalSampler, nullptr);
        vkDestroySampler(device.GetDevice(), m_ComparisonShadowSampler, nullptr);
        vkDestroySampler(device.GetDevice(), m_LinearShadowSampler, nullptr);
    });
}

void VulkanBackend::InitDescriptors()
{
    const uint32_t FIF = FRAMES_IN_FLIGHT;
    const uint32_t TEX_ARRAY_COUNT = static_cast<uint32_t>(TextureArrayID::MAX_ENUM);
    VkDescriptorPoolSize poolSizes[9] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FIF },                           // Uniform buffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 4 },               // 4x GBuffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * TEX_ARRAY_COUNT }, // MAXx Texture arrays
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 3},                // HDR, LDR, Bloom samplers
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 2 },               // Shadow array comparison, shadow array regular
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF },                            // HDR storage image
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FIF * 4 },                       // Lights, Light indices, Tile counts, Shadowcasters
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF },                            // PingPong Buffer
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF * MAX_BLOOM_MIPS },           // Bloom Downsample pyramid
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES_IN_FLIGHT * 5, // Dummy set and sets 0..3
        .poolSizeCount = 9,
        .pPoolSizes = poolSizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device.GetDevice(), &poolInfo, nullptr, &m_DescriptorPool));
    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorPool(device.GetDevice(), m_DescriptorPool, nullptr);
    });

    // ================================================== Create Dummy Set ==================================================

    VkDescriptorSetLayoutCreateInfo layoutInfoDummy = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfoDummy, nullptr, &m_DummyDescriptorLayout));
    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorSetLayout(device.GetDevice(), m_DummyDescriptorLayout, nullptr);
    });

    // ================================================== Create Set 0 ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[3] = {
            { // Uniform buffer
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // Light buffer
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            },
            { // Shadow buffer
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 3,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr, &m_DescriptorLayout0));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device.GetDevice(), m_DescriptorLayout0, nullptr);
        });
    }

    // ================================================== Create Set 1 ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[6] = {
            { // Texture arrays
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = TEX_ARRAY_COUNT,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
            },
            { // Shadow array (comparison)
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            },
            { // Shadow array
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            },
            { // HDR sampler
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // LDR sampler
                .binding = 4,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
            },
            { // Bloom sampler
                .binding = 5,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 6,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr, &m_DescriptorLayout1));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device.GetDevice(), m_DescriptorLayout1, nullptr);
        });
    }

    // ================================================== Create Set 2 ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[4] = {
            { // Light indices buffer
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // Light tile counts buffer
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // GBuffer array
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 4,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // HDR image
                .binding = 3,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            }
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 4,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr, &m_DescriptorLayout2));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device.GetDevice(), m_DescriptorLayout2, nullptr);
        });
    }

    // ================================================== Create Set 3 ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[2] = {
            { // Ping Pong Buffer for blur
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            { // Downsample Pyramid
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = MAX_BLOOM_MIPS,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            }
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr, &m_DescriptorLayout3));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device.GetDevice(), m_DescriptorLayout3, nullptr);
        });
    }

    // ================================================== Allocate Sets ==================================================

    for (FrameData& frame : frames) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_DummyDescriptorLayout
        };
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &frame.dummyDescriptorSet));

        allocInfo.pSetLayouts = &m_DescriptorLayout0;
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &frame.descriptorSet0));

        allocInfo.pSetLayouts = &m_DescriptorLayout1;
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &frame.descriptorSet1));

        allocInfo.pSetLayouts = &m_DescriptorLayout2;
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &frame.descriptorSet2));

        allocInfo.pSetLayouts = &m_DescriptorLayout3;
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &frame.descriptorSet3));
    }

    UpdateDescriptors();
}

void VulkanBackend::UpdateDescriptors()
{
    for (FrameData& frame : frames) {

        // ================================================== Write Set 0 ==================================================

        {
            VkDescriptorBufferInfo uniformBufferInfo = {
                .buffer = frame.uniformBuffer.buffer,
                .range = sizeof(PerFrameUniforms)
            };

            VkDescriptorBufferInfo lightBufferInfo = {
                .buffer = frame.lightBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkDescriptorBufferInfo shadowBufferInfo = {
                .buffer = frame.shadowBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkWriteDescriptorSet writes[3] = {
                { // Uniform buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &uniformBufferInfo
                },
                { // Light buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &lightBufferInfo
                },
                { // Shadow buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &shadowBufferInfo
                },
            };

            vkUpdateDescriptorSets(device.GetDevice(), 3, writes, 0, nullptr);
        }

        // ================================================== Write Set 1 ==================================================

        {
            VkDescriptorImageInfo hdrSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_LightingTarget->GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo ldrSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_ToneMapTarget->GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo bloomSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_BloomPyramidTargets[0]->GetView(), // Top level only (accumulation result)
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkWriteDescriptorSet writes[3] = {
                // Binding 0 = Texture array - not written here
                // Binding 1 = Shadow array (comparison) - not written here
                // Binding 2 = Shadow array - not written here
                { // HDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &hdrSamplerInfo
                },
                { // LDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 4,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &ldrSamplerInfo
                },
                { // Bloom sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 5,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &bloomSamplerInfo
                }
            };

            vkUpdateDescriptorSets(device.GetDevice(), 3, writes, 0, nullptr);
        }

        // ================================================== Write Set 2 ==================================================

        {
            VkDescriptorBufferInfo lightIndexBufferInfo = {
                .buffer = frame.lightIndexBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkDescriptorBufferInfo lightTileCountBufferInfo = {
                .buffer = frame.lightTileCountBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkDescriptorImageInfo gBufferArrayInfo[4] = {
                { m_TextureSampler, m_AlbedoTarget->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_NormalSampler, m_NormalTarget->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_TextureSampler, m_MaterialTarget->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_TextureSampler, m_DepthTarget->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
            };

            VkDescriptorImageInfo hdrStorageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_LightingTarget->GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };

            VkWriteDescriptorSet writes[4] = {
                { // Light Indices buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &lightIndexBufferInfo
                },
                { // Light Tile Counts buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &lightTileCountBufferInfo
                },
                { // GBuffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 2,
                    .descriptorCount = 4,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = gBufferArrayInfo
                },
                { // HDR storage image
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &hdrStorageInfo
                },
            };

            vkUpdateDescriptorSets(device.GetDevice(), 4, writes, 0, nullptr);
        }

        // ================================================== Write Set 3 ==================================================

        {
            VkDescriptorImageInfo bloomPingPongImageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_BloomPingPongTarget->GetView(),
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
            VkDescriptorImageInfo downsampleImageInfos[MAX_BLOOM_MIPS] = {};
            for (uint32_t i = 0; i < m_BloomPyramidMipCount; i++) {
                downsampleImageInfos[i] = {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = m_BloomPyramidTargets[i]->GetView(),
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL
                };
            }

            VkWriteDescriptorSet writes[2] = {
                { // Blur Ping Pong Image
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet3,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &bloomPingPongImageInfo
                },
                { // Downsample Pyramid
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet3,
                    .dstBinding = 1,
                    .descriptorCount = m_BloomPyramidMipCount,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = downsampleImageInfos
                },
            };
            vkUpdateDescriptorSets(device.GetDevice(), 2, writes, 0, nullptr);
        }
    }
}

void VulkanBackend::InitPipelines()
{
    m_DepthPipeline.SetName("Depth")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddPushConstant<VertexPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
            .AddPushConstant<FragmentPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddShader(ShaderKind::VERTEX, "depth.vert")
            .AddShader(ShaderKind::FRAGMENT, "depth.frag")
            .SetDepthWriteAttachment(*m_DepthTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .SetBounds(m_Viewport, m_ScissorRect, m_Extent)
            .SetCulling(VK_CULL_MODE_BACK_BIT)
            .SetDepthFlags(EnumBase(PipelineDepthFlag::WRITE) | EnumBase(PipelineDepthFlag::TEST))
            .SetDepthCompare(VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetVertexInput(Vertex::GetDepthVertexDesc())
            .Build();
    m_DepthPipeline.EnqueueCleanup(m_MainDeleter);

    m_ShadowPipeline.SetName("Shadow")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddPushConstant<ShadowPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
            .AddShader(ShaderKind::VERTEX, "shadow.vert")
            .SetDynamicDepthAttachment(settings.depthFormat)
            .SetBounds(m_ShadowViewport, m_ShadowScissorRect, m_ShadowExtent)
            .SetCulling(VK_CULL_MODE_BACK_BIT)
            .SetDepthFlags(EnumBase(PipelineDepthFlag::WRITE) | EnumBase(PipelineDepthFlag::TEST))
            .SetDepthCompare(VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetVertexInput(Vertex::GetDepthVertexDesc())
            .Build();
    m_ShadowPipeline.EnqueueCleanup(m_MainDeleter);

    m_GBufferPipeline.SetName("GBuffer")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddPushConstant<VertexPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
            .AddPushConstant<FragmentPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddShader(ShaderKind::VERTEX, "gBuffer.vert")
            .AddShader(ShaderKind::FRAGMENT, "gBuffer.frag")
            .AddColorAttachment(*m_AlbedoTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .AddColorAttachment(*m_NormalTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .AddColorAttachment(*m_MaterialTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .SetDepthReadAttachment(*m_DepthTarget, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE)
            .SetBounds(m_Viewport, m_ScissorRect, m_Extent)
            .SetCulling(VK_CULL_MODE_BACK_BIT)
            .SetDepthFlags(EnumBase(PipelineDepthFlag::TEST))
            .SetDepthCompare(VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetVertexInput(Vertex::GetVertexDesc())
            .Build();
    m_GBufferPipeline.EnqueueCleanup(m_MainDeleter);

    m_TransparencyPipeline.SetName("Transparency")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddPushConstant<VertexPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
            .AddPushConstant<FragmentPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddShader(ShaderKind::VERTEX, "transparency.vert")
            .AddShader(ShaderKind::FRAGMENT, "transparency.frag")
            .AddColorAttachment(*m_LightingTarget, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE)
            .SetDepthReadAttachment(*m_DepthTarget, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE)
            .SetBounds(m_Viewport, m_ScissorRect, m_Extent)
            .SetCulling(VK_CULL_MODE_NONE)
            .SetDepthFlags(EnumBase(PipelineDepthFlag::TEST))
            .SetDepthCompare(VK_COMPARE_OP_LESS_OR_EQUAL)
            .EnableBlending(true)
            .SetVertexInput(Vertex::GetVertexDesc())
            .Build();
    m_TransparencyPipeline.EnqueueCleanup(m_MainDeleter);

    m_ToneMapPipeline.SetName("Tone Map")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddPushConstant<ToneMapPushConstants>(VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddShader(ShaderKind::VERTEX, "fullscreen.vert")
            .AddShader(ShaderKind::FRAGMENT, "tonemap.frag")
            .AddColorAttachment(*m_ToneMapTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE)
            .SetBounds(m_Viewport, m_ScissorRect, m_Extent)
            .SetCulling(VK_CULL_MODE_BACK_BIT)
            .SetDepthCompare(VK_COMPARE_OP_ALWAYS)
            .Build();
    m_ToneMapPipeline.EnqueueCleanup(m_MainDeleter);

    m_AntiAliasingPipeline.SetName("Anti Aliasing")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::GRAPHICS)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddShader(ShaderKind::VERTEX, "fullscreen.verte")
            .AddShader(ShaderKind::FRAGMENT, "antialias.frag")
            .SetDynamicColorAttachment(device.GetSwapchainFormat())
            .SetBounds(m_Viewport, m_ScissorRect, m_Extent)
            .SetCulling(VK_CULL_MODE_BACK_BIT)
            .SetDepthCompare(VK_COMPARE_OP_ALWAYS)
            .Build();
    m_AntiAliasingPipeline.EnqueueCleanup(m_MainDeleter);

    // ================================================== Compute Pipelines ==================================================

    m_LightCullingPipeline.SetName("Light Culling")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddShader(ShaderKind::COMPUTE, "lightCulling.comp")
            .Build();
    m_LightCullingPipeline.EnqueueCleanup(m_MainDeleter);

    m_LightingPipeline.SetName("Lighting")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DescriptorLayout1)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddShader(ShaderKind::COMPUTE, "lighting.comp")
            .Build();
    m_LightingPipeline.EnqueueCleanup(m_MainDeleter);

    m_BloomLightExtractionPipeline.SetName("Bloom (Light Extraction)")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddDescriptorLayout(m_DescriptorLayout3)
            .AddPushConstant<BloomPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "lightExtraction.comp")
            .Build();
    m_BloomLightExtractionPipeline.EnqueueCleanup(m_MainDeleter);

    m_BloomDownsamplePipeline.SetName("Bloom (Downsample)")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddDescriptorLayout(m_DescriptorLayout3)
            .AddPushConstant<BloomPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "downsample.comp")
            .Build();
    m_BloomDownsamplePipeline.EnqueueCleanup(m_MainDeleter);

    m_BloomHorizontalBlurPipeline.SetName("Bloom (Horizontal Blur)")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddDescriptorLayout(m_DescriptorLayout3)
            .AddPushConstant<BloomPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "horizontalBlur.comp")
            .Build();
    m_BloomHorizontalBlurPipeline.EnqueueCleanup(m_MainDeleter);

    m_BloomVerticalBlurPipeline.SetName("Bloom (Vertical Blur)")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddDescriptorLayout(m_DescriptorLayout3)
            .AddPushConstant<BloomPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "verticalBlur.comp")
            .Build();
    m_BloomVerticalBlurPipeline.EnqueueCleanup(m_MainDeleter);

    m_BloomAccumulatePipeline.SetName("Bloom (Upscale / Accumulate)")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout0)
            .AddDescriptorLayout(m_DummyDescriptorLayout)
            .AddDescriptorLayout(m_DescriptorLayout2)
            .AddDescriptorLayout(m_DescriptorLayout3)
            .AddPushConstant<BloomPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "accumulate.comp")
            .Build();
    m_BloomAccumulatePipeline.EnqueueCleanup(m_MainDeleter);
}

#ifdef PROFILING
void VulkanBackend::InitProfiling()
{
    std::string headings[HEADING_COUNT] = { "Depth", "GBuffer", "Shadow", "Culling", "Lighting", "Transparency", "Bloom", "Tonemap", "AA", "Total" };
    PROFILER_BEGIN_GPU_SESSION(headings, HEADING_COUNT);

    if (m_PhysicalDeviceProperties.limits.timestampPeriod == 0 || !m_PhysicalDeviceProperties.limits.timestampComputeAndGraphics) {
        ERROR("Physical device doesn't support timestamps");

        uint32_t propertyCount = 1;
        VkQueueFamilyProperties graphicsQueueProperties;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &propertyCount, &graphicsQueueProperties);
        if (graphicsQueueProperties.timestampValidBits == 0) {
            ERROR("Graphics queue doesn't support timestamps");
        }

        abort();
    }

    for (FrameData& frame : frames) {
        frame.queryCount = HEADING_COUNT * 2;

        VkQueryPoolCreateInfo queryPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = frame.queryCount,
        };
        vkCreateQueryPool(device, &queryPoolInfo, nullptr, &frame.queryPool);

        frame.queryTimestamps.resize(frame.queryCount);

        submitter.ImmediateSubmit(device, graphicsQueue, [=](VkCommandBuffer commandBuffer) {
            vkCmdResetQueryPool(commandBuffer, frame.queryPool, 0, frame.queryTimestamps.size());
        });

        m_MainDeleter.push_back([=, this] {
            vkDestroyQueryPool(device, frame.queryPool, nullptr);
        });
    }
}
#endif
