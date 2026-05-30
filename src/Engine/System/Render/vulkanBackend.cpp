#include "vulkanBackend.h"
#include "Component/shadowcaster.h"
#include "ECS/ecsTypes.h"
#include "System/Render/backendTypes.h"

#include <algorithm>
#include <alloca.h>
#include <cstdint>
#include <functional>
#include <ranges>

#include <vulkan/vulkan_core.h>
#include <VkBootstrap/VkBootstrap.h>
#include <GLFW/glfw3.h>
#define VMA_IMPLEMENTATION
#ifdef DEBUG
    #define VMA_LEAK_LOG_FORMAT(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
    #define VMA_ASSERT_LEAK(expr) if (!(expr)) { abort(); }
#endif
#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#include "Component/camera.h"
#include "Component/light.h"
#include "Component/material.h"
#include "Component/mesh.h"

#include "Geometry/frustum.h"
#include "Geometry/vertex.h"

#include "Util/allocator.h"
#include "Util/flags.h"
#include "Util/profiler.h"

#include "handle.h"

#ifdef PROFILING 
    #include "Util/profiler.h"

    const uint32_t HEADING_COUNT = 10;
    
    static uint32_t s_GpuTimestampIndex = 0;
    #define GPU_PROFILING_BEGIN_FRAME() s_GpuTimestampIndex = 0
    #define GPU_PROFILING_TIMESTAMP(cmd) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, s_GpuTimestampIndex++)
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
    VK_CHECK(vkDeviceWaitIdle(device));

    for (auto it = m_MainDeleter.rbegin(); it != m_MainDeleter.rend(); it++) (*it)();
    m_MainDeleter.clear();

    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) (*it)();
    m_DynamicDeleter.clear();

    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
    vkDestroyInstance(m_Instance, nullptr);
}

void VulkanBackend::Init(struct GLFWwindow *window, Config &config)
{
    m_SortingBuffer.reserve(MAX_ENTITIES);

    m_PresentMode = config.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    m_Extent = { config.windowWidth, config.windowHeight };
    m_Viewport = {
        .x = 0.0,
        .y = 0.0,
        .width = static_cast<float>(m_Extent.width),
        .height = static_cast<float>(m_Extent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };
    m_ScissorRect = { 
        .offset = {0, 0}, 
        .extent = m_Extent 
    };

    InitVulkan(window);
    InitDynamics();
    InitSamplers();
    InitBuffers();
    InitDescriptors();
    InitPipelines();
    InitMiscBuffers();
    InitMiscDescriptors();
    InitMiscPipelines();

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

    const uint32_t MINIMUM_SIZES[TEXTURE_ARRAY_MAX_ENUM] = { 3, 1, 1, 1 };
    VkDescriptorImageInfo arrayImageInfos[TEXTURE_ARRAY_MAX_ENUM] = {};

    for (uint32_t arrayId = 0; arrayId < TEXTURE_ARRAY_MAX_ENUM; arrayId++) {
        frontend.textureArrays[arrayId].Init(std::max(info.arrayLayers[arrayId], MINIMUM_SIZES[arrayId]), *this);

        arrayImageInfos[arrayId] = {
            .sampler = m_TextureSampler,
            .imageView = frontend.textureArrays[arrayId].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    };

    VkWriteDescriptorSet writes[FRAMES_IN_FLIGHT * 2] = {};
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet1,
            .dstBinding = 0,
            .descriptorCount = TEXTURE_ARRAY_MAX_ENUM,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = arrayImageInfos
        };
    }
    vkUpdateDescriptorSets(device, FRAMES_IN_FLIGHT, writes, 0, nullptr);

    // ================================================== Shadow Arrays ==================================================

    frontend.maxShadows = info.maxShadows;
    frontend.shadowArray.Init(std::max(info.maxShadows * SHADOW_CASCADE_COUNT, 1u), *this);
    VkDescriptorImageInfo shadowComparisonImageInfo = {
        .sampler = m_ComparisonShadowSampler,
        .imageView = frontend.shadowArray.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkDescriptorImageInfo shadowImageInfo = {
        .sampler = m_LinearShadowSampler,
        .imageView = frontend.shadowArray.view,
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
            .pImageInfo = &shadowComparisonImageInfo
        };
        writes[FRAMES_IN_FLIGHT + i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet1,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &shadowImageInfo
        };
    }
    vkUpdateDescriptorSets(device, FRAMES_IN_FLIGHT, writes, 0, nullptr);
}

void VulkanBackend::CleanupFrontend(GraphicsFrontend& frontend)
{
    frontend.camera = INVALID_HANDLE;

    for (TextureArray& array : frontend.textureArrays) {
        array.Cleanup(device, allocator);
    }

    if (frontend.shadowArray.initialized) {
        frontend.shadowArray.Cleanup(device, allocator);
    }
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

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));

    uint32_t imageIndex;
    VkResult acquireErr = vkAcquireNextImageKHR(device, m_Swapchain, UINT64_MAX, frame.acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquireErr == VK_ERROR_OUT_OF_DATE_KHR || acquireErr == VK_SUBOPTIMAL_KHR) {
        RequestResize(frontend);
        return;
    }
    VK_CHECK(acquireErr);

    SwapchainImageData &swapchainImage = m_Images[imageIndex];
    if (swapchainImage.flightFence != VK_NULL_HANDLE && swapchainImage.flightFence != frame.renderFence) {
        VK_CHECK(vkWaitForFences(device, 1, &swapchainImage.flightFence, VK_TRUE, UINT64_MAX));
    }
    swapchainImage.flightFence = frame.renderFence;

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
    std::memset(frame.shadowBuffer.data, 0, MAX_SHADOWCASTERS * sizeof(Shadowcaster));
    std::memcpy(frame.shadowBuffer.data, shadowcasters, shadowCount * sizeof(Shadowcaster));

     // Zero light culling outputs
    uint32_t tilesX = (m_Extent.width + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
    uint32_t tilesY = (m_Extent.height + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
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
        .shadowSize = glm::ivec2(SHADOW_RESOLUTION),
    };
    uniforms->lightCulling = {
        .tileSize = COMPUTE_TILE_SIZE,
        .tilesX = tilesX,
        .tilesY = tilesY,
        .maxLightsPerTile = MAX_LIGHTS_PER_TILE
    };

    // ================================================== Organize Entities ==================================================

    Frustum cameraFrustum = Frustum(camera.projection * camera.view);
    auto PredicateIsVisible = [ecs, &cameraFrustum](Entity e) {
        glm::mat4x4 model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs);
        CentreExtents centreExtents = CentreExtents(ecs->GetComponent<Mesh>(e).bounds);
        return cameraFrustum.Intersects(centreExtents.ToWorldSpace(model));
    };
    auto PredicateIsTransparent = [ecs](Entity e) {
        return FLAGS_CONTAIN(ecs->GetComponent<Material>(e).flags, MATERIAL_FLAG_TRANSPARENT);
    };

    // Frustum Culled
    auto visibleEntities = entities | std::views::filter(PredicateIsVisible);

    // Culled + Opaque
    auto visibleOpaqueEntities = visibleEntities | std::views::filter(std::not_fn(PredicateIsTransparent));

    // Culled + Sorted + Transparent
    m_SortingBuffer.resize(0);
    for (Entity e : visibleEntities) {
        // Skip opaque
        if (!FLAGS_CONTAIN(ecs->GetComponent<Material>(e).flags, MATERIAL_FLAG_TRANSPARENT)) {
            continue;
        }

        const Transform& transform = ecs->GetComponent<Transform>(e);
        glm::vec3 meshCentre = ecs->GetComponent<Mesh>(e).bounds.Centroid();

        glm::vec3 worldCentre = transform.GlobalRotation(ecs) * (meshCentre * transform.GlobalScale(ecs)) + transform.GlobalTranslation(ecs);
        glm::vec3 difference = worldCentre - glm::vec3(cameraPos);
        float distanceSquared = glm::dot(difference, difference);
        m_SortingBuffer.emplace_back(distanceSquared, e);
    }
    std::sort(m_SortingBuffer.begin(), m_SortingBuffer.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    const auto& visibleTransparentEntities = m_SortingBuffer;

    // ================================================== Prepare to Draw ==================================================

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_ScissorRect);

    { // ================================================== Depth Pre-Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);
        
        // Transition depth image to depth attachment
        VkImageMemoryBarrier depthBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image = m_DepthTarget.image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

        // Begin rendering
        VkRenderingAttachmentInfoKHR depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = m_DepthTarget.view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { 
                .depthStencil = { 1.0, 0 } 
            }
        };
        VkRenderingInfoKHR renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = { { 0, 0 }, m_Extent },
            .layerCount = 1,
            .pDepthAttachment = &depthAttachment,
        };
        m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

        // Render
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthPipeline);
        VkDescriptorSet descriptorSets[2] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_MinimalPipelineLayout, 
                0, 2, descriptorSets, 0, nullptr);
        for (const Entity e : visibleOpaqueEntities) {
            VertexPushConstants vertexConstants = {
                .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_MinimalPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                    0, sizeof(VertexPushConstants), 
                    &vertexConstants);

            FragmentPushConstants fragmentConstants = { 
                .material = ecs->GetComponent<Material>(e).Pack(),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_MinimalPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                    sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                    &fragmentConstants);

            const Mesh& mesh = ecs->GetComponent<Mesh>(e);
            VkDeviceSize offsets = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
            vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
        }
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition depth attachment to read only
        depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);
        
    } // ================================================== (END) Depth Pre-Pass ==================================================

    { // ================================================== GBuffer Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition color images to attachment
        VkImageMemoryBarrier colorBarriers[3] = {};
        VkImage colorImages[3] = { m_AlbedoTarget.image.image, m_NormalTarget.image.image, m_MaterialTarget.image.image };
        for (uint32_t i = 0; i < 3; i++) {
            colorBarriers[i] = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = colorImages[i],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1
                }
            };
        }
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                0, 0, nullptr, 0, nullptr, 3, colorBarriers);

        // Begin Rendering
        VkRenderingAttachmentInfoKHR colorAttachments[3] = {};

        colorAttachments[0] = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = m_AlbedoTarget.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { { 0.0, 0.0, 0.0, 0.0 } } }
        };

        colorAttachments[1] = colorAttachments[0];
        colorAttachments[1].imageView = m_NormalTarget.view;
        colorAttachments[1].clearValue.color = { { 0.5, 0.5, 1.0, 0.0 } };

        colorAttachments[2] = colorAttachments[0];
        colorAttachments[2].imageView = m_MaterialTarget.view;
        colorAttachments[2].clearValue.color = { { 0.0, 0.0, 0.0, 0.0 } };

        VkRenderingAttachmentInfoKHR depthReadAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = m_DepthTarget.view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE
        };

        VkRenderingInfoKHR renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = { { 0, 0 }, m_Extent },
            .layerCount = 1,
            .colorAttachmentCount = 3,
            .pColorAttachments = colorAttachments,
            .pDepthAttachment = &depthReadAttachment,
        };
        m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

        // Render
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufferPipeline);
        for (const Entity e : visibleOpaqueEntities) {
            VertexPushConstants vertexConstants = {
                .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_MinimalPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                    0, sizeof(VertexPushConstants), 
                    &vertexConstants);

            FragmentPushConstants fragmentConstants = { 
                .material = ecs->GetComponent<Material>(e).Pack(),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_MinimalPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                    sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                    &fragmentConstants);

            const Mesh& mesh = ecs->GetComponent<Mesh>(e);
            VkDeviceSize offsets = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
            vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
        }
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition color attachments to shader readable 
        for (int32_t i = 0; i < 3; i++) {
            colorBarriers[i].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            colorBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            colorBarriers[i].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 3, colorBarriers);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) GBuffer Pass ==================================================

    { // ================================================== Shadowcaster Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        if (frontend.maxShadows > 0) {
            // Transition shadow array to attachment
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .image = frontend.shadowArray.image.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .levelCount = 1,
                    .layerCount = frontend.shadowArray.layerCount,
                }
            };
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Need to switch to the new extent
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_ShadowViewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_ShadowScissorRect);

            auto opaqueEntities = entities | std::views::filter(std::not_fn(PredicateIsTransparent));

            for (uint32_t i = 0; i < shadowCount; i++) {
                const Shadowcaster& shadowcaster = shadowcasters[i];

                for (uint32_t j = 0; j < SHADOW_CASCADE_COUNT; j++) {
                    const Shadowcaster::Cascade& cascade = shadowcaster.cascades[j];
                    if (cascade.far < 0.0) {
                        break;
                    }

                    VkRenderingAttachmentInfoKHR depthAttachment = {
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                        .imageView = frontend.shadowArray.arrayViews[cascade.shadowMapIndex],
                        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue = {
                            .depthStencil = { 1.0, 0 }
                        }
                    };

                    VkRenderingInfoKHR renderingInfo = {
                        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
                        .renderArea = { { 0, 0 }, m_ShadowExtent },
                        .layerCount = 1,
                        .pDepthAttachment = &depthAttachment 
                    };

                    m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

                    // Render
                    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);
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
                                m_ShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                                0, sizeof(ShadowPushConstants), 
                                &constants);

                        VkDeviceSize offsets = 0;
                        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
                    }
                    m_PFNCmdEndRendering(frame.commandBuffer);
                }
            }

            // Reset extent
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_ScissorRect);

            // Transition shadow array to shader readable
            barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Shadowcaster Pass ==================================================

    { // ================================================== Light Culling Compute Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Dispatch Compute
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightCullingPipeline);
        VkDescriptorSet descriptorSets[3] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet,
            frame.descriptorSet2 };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_COMPUTE, m_LightCullingPipelineLayout, 
                0, 3, descriptorSets, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Light Culling Compute Pass ==================================================

    { // ================================================== Lighting Compute Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition HDR image to general
        VkImageMemoryBarrier hdrBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = m_LightingTarget.image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);

        // Dispatch Compute
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipeline);
        VkDescriptorSet descriptorSets[3] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1, 
            frame.descriptorSet2 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipelineLayout, 
                0, 3, descriptorSets, 0, nullptr);
        vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Lighting Compute Pass ==================================================

    { // ================================================== Transparency Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        if (!visibleTransparentEntities.empty()) {
            // Transition HDR image to attachment
            VkImageMemoryBarrier hdrBarrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .image = m_LightingTarget.image.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1
                }
            };
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);

            // Begin Rendering
            VkRenderingAttachmentInfoKHR colorAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE
            };

            VkRenderingAttachmentInfoKHR depthReadAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                .imageView = m_DepthTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE
            };

            VkRenderingInfoKHR renderingInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
                .renderArea = { { 0, 0 }, m_Extent },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthReadAttachment
            };
            m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

            // Render
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TransparencyPipeline);
            for (const auto& [_, e] : visibleTransparentEntities) {
                VertexPushConstants vertexConstants = {
                    .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_LightingPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                        0, sizeof(VertexPushConstants), 
                        &vertexConstants);

                FragmentPushConstants fragmentConstants = { 
                    .material = ecs->GetComponent<Material>(e).Pack(),
                };
                vkCmdPushConstants(frame.commandBuffer, 
                        m_LightingPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                        sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                        &fragmentConstants);

                const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                VkDeviceSize offsets = 0;
                vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
            }
            m_PFNCmdEndRendering(frame.commandBuffer);

            // Transition hdr image from attachment to general
            hdrBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            hdrBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);
        }

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Transparency ==================================================

    { // ================================================== Bloom Compute ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        VkMemoryBarrier writeBarrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
        };

        // Transition ping-pong image to general
        VkImageMemoryBarrier barrierToGeneral = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .image = m_BloomPingPongTarget.image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &barrierToGeneral);

        // Transition pyramid to general
        VkImageMemoryBarrier pyramidBarriers[MAX_BLOOM_MIPS] = {};
        for (uint32_t i = 0; i < m_BloomPyramidTarget.count; i++) {
            pyramidBarriers[i] = barrierToGeneral;
            pyramidBarriers[i].srcAccessMask = 0;
            pyramidBarriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            pyramidBarriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            pyramidBarriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            pyramidBarriers[i].image = m_BloomPyramidTarget.images[i].image;
        }
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, m_BloomPyramidTarget.count, pyramidBarriers);

        // Downsample hdr image into pyramid[0]
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomDownsamplePipeline);
        VkDescriptorSet descriptorSets[4] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet, 
            frame.descriptorSet2, 
            frame.descriptorSet3, 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomPipelineLayout, 
                0, 4, descriptorSets, 0, nullptr);

        // Dispatch compute
        BloomPushConstants constants = {
            .currentIndex = 0,
        };
        vkCmdPushConstants(frame.commandBuffer, 
                m_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                0, sizeof(BloomPushConstants), 
                &constants);
        uint32_t w = std::max(m_Extent.width >> 1, 1u);
        uint32_t h = std::max(m_Extent.height >> 1, 1u);
        uint32_t groupsX = (w + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
        uint32_t groupsY = (h + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
        vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

        // Wait first downsample to complete
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

        // In place light extraction on pyramid[0]

        // Dispatch Compute
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomLightExtractionPipeline);
        vkCmdDispatch(frame.commandBuffer, tilesX, tilesY, 1);

        // Wait for bright pass to complete
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

        // Downsample Pyramid

        // Dispatch Compute for each layer
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomDownsamplePipeline);
        for (uint32_t i = 1; i < m_BloomPyramidTarget.count; i++) {
            BloomPushConstants constants = {
                .currentIndex = i,
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                    0, sizeof(BloomPushConstants), 
                    &constants);

            uint32_t w = std::max(m_Extent.width >> (i + 1), 1u);
            uint32_t h = std::max(m_Extent.height >> (i + 1), 1u);
            uint32_t groupsX = (w + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
            uint32_t groupsY = (h + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
            vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

            // Wait for current layer to downsample before dispatching next
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
        }

        // Blur 

        // Dispatch Compute for each layer
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomHorizontalBlurPipeline);
        for (uint32_t i = 0; i < m_BloomPyramidTarget.count; i++) {
            BloomPushConstants constants = {
                .currentIndex = i,
                .blurKernelRadius = BLOOM_BLUR_RADIUS,
                .blurKernelWeights = { 0.007, 0.032, 0.059, 0.079, 0.088, 0.079, 0.059, 0.032, 0.007 }, // Gaussian
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                    0, sizeof(BloomPushConstants), 
                    &constants);

            const uint32_t BLUR_COMPUTE_GROUP_SIZE = 256;
            uint32_t w = std::max(m_Extent.width >> (i + 1), 1u);
            uint32_t h = std::max(m_Extent.height >> (i + 1), 1u);

            uint32_t groupsX = (w + BLUR_COMPUTE_GROUP_SIZE - 1) / BLUR_COMPUTE_GROUP_SIZE;
            uint32_t groupsY = h;
            vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

            // Wait for horizontal blurs to finish before the vertical ones are dispatched
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);

            groupsX = w;
            groupsY = (h + BLUR_COMPUTE_GROUP_SIZE - 1) / BLUR_COMPUTE_GROUP_SIZE;
            vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

            // Wait for vertical blurs to finish before next level or upsampling
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
        }

        // Upsample / Accumulate

        // Dispatch Compute for each layer (starting from the second lowest level)
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomAccumulatePipeline);
        // NOTE: Using int32_t instead of uint32_t for i because the decrement causes underflow when i = 0
        for (int32_t i = m_BloomPyramidTarget.count - 2; i >= 0; i--) { // Shader upsamples the layer above (smaller)
            BloomPushConstants constants = {
                .currentIndex = static_cast<uint32_t>(i),
                .accumulationFactor = 0.1,
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                    0, sizeof(BloomPushConstants), 
                    &constants);

            const uint32_t UPSAMPLE_COMPUTE_GROUP_SIZE = 8;
            uint32_t w = std::max(m_Extent.width >> (i + 1), 1u);
            uint32_t h = std::max(m_Extent.height >> (i + 1), 1u);

            uint32_t groupsX = (w + UPSAMPLE_COMPUTE_GROUP_SIZE - 1) / UPSAMPLE_COMPUTE_GROUP_SIZE;
            uint32_t groupsY = h;
            vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

            // Wait for current layer to accumulate before starting next
            vkCmdPipelineBarrier(frame.commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 1, &writeBarrier, 0, nullptr, 0, nullptr);
        }

        // Transition highest pyramid level (where the accumulated bloom is) to shader readable
        VkImageMemoryBarrier bloomBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = m_BloomPyramidTarget.images[0].image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &bloomBarrier);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Bloom Compute ==================================================

    { // ================================================== Tone-Mapping Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition target to color attachment
        VkImageMemoryBarrier toneMapBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = m_ToneMapTarget.image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &toneMapBarrier);

        // Transition HDR image to shader readonly
        toneMapBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toneMapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toneMapBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toneMapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toneMapBarrier.image = m_LightingTarget.image.image;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 0, nullptr, 0, nullptr, 1, &toneMapBarrier);

        // Begin rendering
        VkRenderingAttachmentInfoKHR colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = m_ToneMapTarget.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { 
                .color = { 1.0, 0.0, 1.0, 1.0 } // Purple for debug purposes
            }
        };
        VkRenderingInfoKHR renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = { { 0, 0 }, m_Extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };
        m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

        // Render
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ToneMapPipeline);
        VkDescriptorSet descriptorSets[2] = { 
            frame.descriptorSet0, 
            frame.descriptorSet1, 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_MinimalPipelineLayout, 
                0, 2, &frame.descriptorSet0, 0, nullptr);
        vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition tone mapped attachment to shader readable 
        toneMapBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toneMapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toneMapBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toneMapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toneMapBarrier.image = m_ToneMapTarget.image.image;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &toneMapBarrier);

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

    } // ================================================== (END) Tone-Mapping Pass ==================================================

    { // ================================================== (SWAPCHAIN) Anti-Aliasing Pass ==================================================

        GPU_PROFILING_TIMESTAMP(frame.commandBuffer);

        // Transition swapchain image to attachment attachment
        VkImageMemoryBarrier swapchainBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = swapchainImage.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);

        // Begin rendering
        VkRenderingAttachmentInfoKHR colorAttachments = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = swapchainImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { 
                .color = { 1.0, 0.0, 1.0, 1.0 } // Purple for debug purposes
            }
        };
        VkRenderingInfoKHR renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = { { 0, 0 }, m_Extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachments,
        };
        m_PFNCmdBeginRendering(frame.commandBuffer, &renderingInfo);

        // Render
        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_AntiAliasingPipeline);
        vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition swapchain attachment to present source
        swapchainBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        swapchainBarrier.dstAccessMask = 0;
        swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);

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
        .pSignalSemaphores = &swapchainImage.renderSemaphore
    };
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.renderFence));

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &swapchainImage.renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_Swapchain,
        .pImageIndices = &imageIndex,
    };
    VkResult presentErr = vkQueuePresentKHR(graphicsQueue, &presentInfo);
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
    vmaCreateBuffer(allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);

    vmaMapMemory(allocator, stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, vertices, count * sizeof(Vertex));
    vmaUnmapMemory(allocator, stagingBuffer.allocation);

    // Create vertex buffer
    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = count * sizeof(Vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateBuffer(allocator, &vertexBufferInfo, &allocInfo, &result.buffer, &result.allocation, nullptr);

    // Copy the buffer to the GPU
    submitter.ImmediateSubmit(device, graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = count * sizeof(Vertex),
        };
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, result.buffer, 1, &copy);
    });

    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    return result;
}

AllocatedTexture VulkanBackend::AllocateTexture(ImageResource& image, GraphicsFrontend& frontend)
{
    // Select the right array
    auto Fits = [](glm::ivec2 imageSize, int32_t arrayResolution) {
        return imageSize.x <= arrayResolution && imageSize.y <= arrayResolution;
    };

    TextureArrayID arrayID = TEXTURE_ARRAY_MAX_ENUM;
    if (FLAGS_CONTAIN(image.flags, IMAGE_FLAG_FONT_ATLAS) && image.size == glm::ivec2(FONT_RESOLUTION)) {
        arrayID = TEXTURE_ARRAY_FONT;
    } else if (FLAGS_CONTAIN(image.flags, IMAGE_FLAG_NON_COLOR)) {
        if (Fits(image.size, DATA_SMALL_RESOLUTION)) {
            arrayID = TEXTURE_ARRAY_DATA_SMALL;
        } else if (Fits(image.size, DATA_LARGE_RESOLUTION)) {
            arrayID = TEXTURE_ARRAY_DATA_LARGE;
        }
    } else if (FLAGS_CONTAIN_ANY(image.flags, IMAGE_FLAG_TRANSPARENT, IMAGE_FLAG_CUTOUT) || image.flags == 0) {
        if (Fits(image.size, COLOR_SMALL_RESOLUTION)) {
            arrayID = TEXTURE_ARRAY_COLOR_SMALL;
        } else if (Fits(image.size, COLOR_LARGE_RESOLUTION)){
            arrayID = TEXTURE_ARRAY_COLOR_LARGE;
        }
    }
    if (arrayID == TEXTURE_ARRAY_MAX_ENUM) {
        FATAL("Texture too large for arrays (" << image.size.x << ", " << image.size.y << ")");
    }

    // Allocate
    uint32_t layerIndex = frontend.textureArrays[arrayID].Allocate(image, *this);
    AllocatedTexture allocation = { arrayID, layerIndex };

    // Generate mips
    GenerateMips(allocation, image, frontend);

    return allocation;
}

AllocatedTexture VulkanBackend::AllocateTexture(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, GraphicsFrontend& frontend)
{
    ImageResource tmp{flags, size, pixels, channels};
    return AllocateTexture(tmp, frontend);
}

void VulkanBackend::Resize(ECS *ecs)
{
    PROFILER_PROFILE_SCOPE("VulkanBackend::Resize");

    INFO("Resized");

    // Check if valid
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(m_PFNGetSurfaceCapabilities(physicalDevice, m_Surface, &capabilities));
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0 || extent.height == 0 || extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
        m_ResizeCameras.clear();
        return;
    }

    // Update extents
    VK_CHECK(vkDeviceWaitIdle(device));
    m_Extent = extent;
    m_Viewport.width = static_cast<float>(m_Extent.width);
    m_Viewport.height = static_cast<float>(m_Extent.height);
    m_ScissorRect.extent = m_Extent;

    // Reset all command buffers
    for (FrameData& frame : frames) {
        VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    }

    // Cleanup old resources
    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) (*it)();
    m_DynamicDeleter.clear();

    // Recreate resources
    InitDynamics();
    UpdateDescriptors();

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
    return frontend.shadowArray.Allocate();
}

void VulkanBackend::InitVulkan(struct GLFWwindow *window)
{
    vkb::InstanceBuilder builder;
    auto builtInstance = builder.request_validation_layers(USE_VALIDATION_LAYERS)
                                .set_app_version(1, 0, 0)
                                .require_api_version(1, 3, 0)
                                .use_default_debug_messenger()
                                .build();

    vkb::Instance vkbInstance = builtInstance.value();
    m_Instance = vkbInstance.instance;
    m_DebugMessenger = vkbInstance.debug_messenger;

    VK_CHECK(glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface));

    vkb::PhysicalDeviceSelector selector(vkbInstance);
    auto vkbPhysicalDevice = selector.set_surface(m_Surface)
                                     .set_minimum_version(1, 3)
                                     .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                                     .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
                                     .set_required_features({ .samplerAnisotropy = VK_TRUE })
                                     .set_required_features_13({ .dynamicRendering = VK_TRUE })
                                     .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                     .require_present()
                                     .select()
                                     .value();
    INFO("Selected physical device: " << vkbPhysicalDevice.name);

    vkb::DeviceBuilder deviceBuilder(vkbPhysicalDevice);
    vkb::Device vkbDevice = deviceBuilder.build().value();
    device = vkbDevice.device;
    physicalDevice = vkbDevice.physical_device;
    vkGetPhysicalDeviceProperties(physicalDevice, &m_PhysicalDeviceProperties);

    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocInfo = {
        .physicalDevice = physicalDevice,
        .device = device,
        .instance = m_Instance,
    };
    vmaCreateAllocator(&allocInfo, &allocator);

    m_PFNGetSurfaceCapabilities = GetFunctionPointer<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    m_PFNCmdBeginRendering = GetFunctionPointer<PFN_vkCmdBeginRenderingKHR>("vkCmdBeginRenderingKHR");
    m_PFNCmdEndRendering = GetFunctionPointer<PFN_vkCmdEndRenderingKHR>("vkCmdEndRenderingKHR");

    VkCommandPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily
    };
    VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &m_CommandPool));
    m_MainDeleter.push_back([=, this] { 
        vkDestroyCommandPool(device, m_CommandPool, nullptr); 
    });

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .commandBufferCount = 1,
    };
    for (FrameData& frame : frames) {
        VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &frame.commandBuffer));
    }

    submitter.Init(device, graphicsQueueFamily);
    m_MainDeleter.push_back([=, this] {
        submitter.Cleanup(device);
    });
}

void VulkanBackend::InitDynamics()
{
    // ================================================== Swapchain ==================================================

    vkb::SwapchainBuilder builder { physicalDevice, device, m_Surface };
    auto vkbSwapchain = builder.use_default_format_selection()
                               .set_desired_present_mode(m_PresentMode)
                               .set_desired_min_image_count(FRAMES_IN_FLIGHT + 1)
                               .set_desired_extent(m_Extent.width, m_Extent.height)
                               .build()
                               .value();

    if (vkbSwapchain.present_mode != m_PresentMode) {
        WARN("Requested present mode unavailable: " << m_PresentMode << ". Fell back to: " << vkbSwapchain.present_mode);
    }

    m_Swapchain = vkbSwapchain.swapchain;
    m_SwapchainFormat = vkbSwapchain.image_format;

    m_DynamicDeleter.push_back([=] { 
        vkb::destroy_swapchain(vkbSwapchain);
    });

    std::vector<VkImage> swapchainImages = vkbSwapchain.get_images().value();
    std::vector<VkImageView> swapchainViews = vkbSwapchain.get_image_views().value();
    m_Images = std::vector<SwapchainImageData>(swapchainImages.size());
    for (size_t i = 0; i < m_Images.size(); i++) {
        m_Images[i] = {
            .image = swapchainImages[i],
            .view = swapchainViews[i],
        };

        m_DynamicDeleter.push_back([=, this] { 
            vkDestroyImageView(device, m_Images[i].view, nullptr); 
        });
    }
    INFO("Using swapchain images: " << m_Images.size());

    // ================================================== Target Defaults ==================================================

    VmaAllocationCreateInfo imageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };
    VkImageCreateInfo imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = { m_Extent.width, m_Extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
    };
    VkImageViewCreateInfo viewCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .subresourceRange = {
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };

    // ================================================== Depth Target ==================================================

    imageCreateInfo.format = DEPTH_FORMAT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_DepthTarget.image.image, &m_DepthTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_DepthTarget.image.image;
    viewCreateInfo.format = DEPTH_FORMAT;
    viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_DepthTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_DepthTarget.view, nullptr);
        vmaDestroyImage(allocator, m_DepthTarget.image.image, m_DepthTarget.image.allocation);
    });

    // ================================================== Albedo Target ==================================================

    imageCreateInfo.format = ALBEDO_FORMAT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_AlbedoTarget.image.image, &m_AlbedoTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_AlbedoTarget.image.image;
    viewCreateInfo.format = ALBEDO_FORMAT;
    viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_AlbedoTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_AlbedoTarget.view, nullptr);
        vmaDestroyImage(allocator, m_AlbedoTarget.image.image, m_AlbedoTarget.image.allocation);
    });

    // ================================================== Normal Target ==================================================

    imageCreateInfo.format = NORMAL_FORMAT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_NormalTarget.image.image, &m_NormalTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_NormalTarget.image.image;
    viewCreateInfo.format = NORMAL_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_NormalTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_NormalTarget.view, nullptr);
        vmaDestroyImage(allocator, m_NormalTarget.image.image, m_NormalTarget.image.allocation);
    });

    // ================================================== Material Target ==================================================

    imageCreateInfo.format = MATERIAL_FORMAT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_MaterialTarget.image.image, &m_MaterialTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_MaterialTarget.image.image;
    viewCreateInfo.format = MATERIAL_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_MaterialTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_MaterialTarget.view, nullptr);
        vmaDestroyImage(allocator, m_MaterialTarget.image.image, m_MaterialTarget.image.allocation);
    });

    // ================================================== Lighting Target ==================================================

    imageCreateInfo.format = LIGHTING_FORMAT;
    imageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_LightingTarget.image.image, &m_LightingTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_LightingTarget.image.image;
    viewCreateInfo.format = LIGHTING_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_LightingTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_LightingTarget.view, nullptr);
        vmaDestroyImage(allocator, m_LightingTarget.image.image, m_LightingTarget.image.allocation);
    });

    // ================================================== Bloom Downsample Target ==================================================

    m_BloomPyramidTarget.count = std::min(static_cast<uint32_t>(std::floor(std::log2(std::max(m_Extent.width, m_Extent.height)))), MAX_BLOOM_MIPS);

    imageCreateInfo.format = BLOOM_FORMAT;
    viewCreateInfo.format = BLOOM_FORMAT;
    for (uint32_t i = 0; i < m_BloomPyramidTarget.count; i++) {
        imageCreateInfo.extent.width = std::max(m_Extent.width >> (i + 1), 1u);
        imageCreateInfo.extent.height = std::max(m_Extent.height >> (i + 1), 1u);
        vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_BloomPyramidTarget.images[i].image, &m_BloomPyramidTarget.images[i].allocation, nullptr);

        viewCreateInfo.image = m_BloomPyramidTarget.images[i].image;
        VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_BloomPyramidTarget.views[i]));

        m_DynamicDeleter.push_back([=, this] {
            vkDestroyImageView(device, m_BloomPyramidTarget.views[i], nullptr);
            vmaDestroyImage(allocator, m_BloomPyramidTarget.images[i].image, m_BloomPyramidTarget.images[i].allocation);
        });
    }

    // ================================================== Bloom Ping Pong Target ==================================================

    imageCreateInfo.extent = { m_Extent.width, m_Extent.height, 1 };
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_BloomPingPongTarget.image.image, &m_BloomPingPongTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_BloomPingPongTarget.image.image;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_BloomPingPongTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_BloomPingPongTarget.view, nullptr);
        vmaDestroyImage(allocator, m_BloomPingPongTarget.image.image, m_BloomPingPongTarget.image.allocation);
    });

    // ================================================== Tone-Map Target ==================================================

    imageCreateInfo.format = TONE_MAP_FORMAT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_ToneMapTarget.image.image, &m_ToneMapTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_ToneMapTarget.image.image;
    viewCreateInfo.format = TONE_MAP_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_ToneMapTarget.view));

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_ToneMapTarget.view, nullptr);
        vmaDestroyImage(allocator, m_ToneMapTarget.image.image, m_ToneMapTarget.image.allocation);
    });

    // ================================================== Sync Structs ==================================================

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    for (FrameData& frame : frames) {
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.acquireSemaphore));

        m_DynamicDeleter.push_back([=, this] {
            vkDestroyFence(device, frame.renderFence, nullptr);
            vkDestroySemaphore(device, frame.acquireSemaphore, nullptr);
        });
    }

    for (SwapchainImageData& image : m_Images) {
        image.flightFence = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &image.renderSemaphore));

        m_DynamicDeleter.push_back([=, this] {
            vkDestroySemaphore(device, image.renderSemaphore, nullptr); 
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
        .size = MAX_SHADOWCASTERS * sizeof(Shadowcaster),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
    };

    uint32_t tilesX = (m_Extent.width + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
    uint32_t tilesY = (m_Extent.height + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
    VkBufferCreateInfo lightIndexBufferInfo = { // Up to MAX_LIGHTS_PER_TILE indices per tile
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = MAX_LIGHTS_PER_TILE * tilesX * tilesY * sizeof(uint32_t),
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
        vmaCreateBuffer(allocator, 
                &uniformBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.uniformBuffer.allocation, &frame.uniformBuffer.data);

        vmaCreateBuffer(allocator, 
                &lightBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.lightBuffer.buffer, &frame.lightBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.lightBuffer.allocation, &frame.lightBuffer.data);

        vmaCreateBuffer(allocator, 
                &shadowBufferInfo, &cpuGpuBufferAllocInfo, 
                &frame.shadowBuffer.buffer, &frame.shadowBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.shadowBuffer.allocation, &frame.shadowBuffer.data);

        vmaCreateBuffer(allocator, 
                &lightIndexBufferInfo, &gpuBufferAllocInfo, 
                &frame.lightIndexBuffer.buffer, &frame.lightIndexBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.lightIndexBuffer.allocation, &frame.lightIndexBuffer.data);

        vmaCreateBuffer(allocator, 
                &lightTileCountBufferInfo, &gpuBufferAllocInfo, 
                &frame.lightTileCountBuffer.buffer, &frame.lightTileCountBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.lightTileCountBuffer.allocation, &frame.lightTileCountBuffer.data);

        m_MainDeleter.push_back([=, this] {
            vmaUnmapMemory(allocator, frame.uniformBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);

            vmaUnmapMemory(allocator, frame.lightBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.lightBuffer.buffer, frame.lightBuffer.allocation);

            vmaUnmapMemory(allocator, frame.shadowBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.shadowBuffer.buffer, frame.shadowBuffer.allocation);

            vmaUnmapMemory(allocator, frame.lightIndexBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.lightIndexBuffer.buffer, frame.lightIndexBuffer.allocation);

            vmaUnmapMemory(allocator, frame.lightTileCountBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.lightTileCountBuffer.buffer, frame.lightTileCountBuffer.allocation);
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
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_TextureSampler));

    // Distinct sampler for normal maps in case I need to adjust anisotropy or mipmapping
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_NormalSampler));

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_OffscreenSampler));

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_GREATER;
    samplerInfo.maxLod = 1.0;
    samplerInfo.mipLodBias = 0.0;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 0.0;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_ComparisonShadowSampler));
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_LinearShadowSampler));

    m_MainDeleter.push_back([=, this] {
        vkDestroySampler(device, m_OffscreenSampler, nullptr);
        vkDestroySampler(device, m_TextureSampler, nullptr);
        vkDestroySampler(device, m_NormalSampler, nullptr);
        vkDestroySampler(device, m_ComparisonShadowSampler, nullptr);
        vkDestroySampler(device, m_LinearShadowSampler, nullptr);
    });
}

void VulkanBackend::InitDescriptors()
{
    const uint32_t FIF = FRAMES_IN_FLIGHT;
    VkDescriptorPoolSize poolSizes[9] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FIF },                                  // Uniform buffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 4 },                      // 4x GBuffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * TEXTURE_ARRAY_MAX_ENUM }, // MAXx Texture arrays
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 3},                       // HDR, LDR, Bloom samplers
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 2 },                      // Shadow array comparison, shadow array regular
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF },                                   // HDR storage image
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FIF * 4 },                              // Lights, Light indices, Tile counts, Shadowcasters
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF },                                   // PingPong Buffer
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF * MAX_BLOOM_MIPS },                  // Bloom Downsample pyramid
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES_IN_FLIGHT * 5, // Dummy set and sets 0..3
        .poolSizeCount = 9,
        .pPoolSizes = poolSizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool));
    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    });

    // ================================================== Create Dummy Set ==================================================

    VkDescriptorSetLayoutCreateInfo layoutInfoDummy = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfoDummy, nullptr, &m_DummyDescriptorLayout));
    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorSetLayout(device, m_DummyDescriptorLayout, nullptr);
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorLayout0));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device, m_DescriptorLayout0, nullptr);
        });
    }

    // ================================================== Create Set 1 ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[6] = {
            { // Texture arrays
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = TEXTURE_ARRAY_MAX_ENUM,
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorLayout1));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device, m_DescriptorLayout1, nullptr);
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorLayout2));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device, m_DescriptorLayout2, nullptr);
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorLayout3));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device, m_DescriptorLayout3, nullptr);
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
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.dummyDescriptorSet));

        allocInfo.pSetLayouts = &m_DescriptorLayout0;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet0));

        allocInfo.pSetLayouts = &m_DescriptorLayout1;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet1));

        allocInfo.pSetLayouts = &m_DescriptorLayout2;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet2));

        allocInfo.pSetLayouts = &m_DescriptorLayout3;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet3));
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

            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        }

        // ================================================== Write Set 1 ==================================================

        {
            VkDescriptorImageInfo hdrSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo ldrSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_ToneMapTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo bloomSamplerInfo = {
                .sampler = m_OffscreenSampler,
                .imageView = m_BloomPyramidTarget.views[0], // Top level only (accumulation result)
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

            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
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
                { m_TextureSampler, m_AlbedoTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_NormalSampler, m_NormalTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_TextureSampler, m_MaterialTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_TextureSampler, m_DepthTarget.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
            };

            VkDescriptorImageInfo hdrStorageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_LightingTarget.view,
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

            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
        }

        // ================================================== Write Set 3 ==================================================

        {
            VkDescriptorImageInfo bloomPingPongImageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_BloomPingPongTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
            VkDescriptorImageInfo downsampleImageInfos[MAX_BLOOM_MIPS] = {};
            for (uint32_t i = 0; i < m_BloomPyramidTarget.count; i++) {
                downsampleImageInfos[i] = {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = m_BloomPyramidTarget.views[i],
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
                    .descriptorCount = m_BloomPyramidTarget.count,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = downsampleImageInfos
                },
            };
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
    }
}

void VulkanBackend::InitPipelines()
{
    // ================================================== Main Push Constants ==================================================

    VkPushConstantRange mainPushConstantRanges[2] = {
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(VertexPushConstants),
        },
        {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = sizeof(VertexPushConstants),
            .size = sizeof(FragmentPushConstants)
        }
    };

    // ================================================== Minimal Layout ==================================================

    {
        VkDescriptorSetLayout minimalLayouts[2] = { 
            m_DescriptorLayout0, 
            m_DescriptorLayout1 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2,
            .pSetLayouts = minimalLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainPushConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_MinimalPipelineLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyPipelineLayout(device, m_MinimalPipelineLayout, nullptr);
        });
    }

    // ================================================== Shadow Layout ==================================================

    {
        VkPushConstantRange shadowPushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(ShadowPushConstants),
        };

        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &shadowPushConstantRange
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_ShadowPipelineLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyPipelineLayout(device, m_ShadowPipelineLayout, nullptr);
        });
    }

    // ================================================== Light Culling Layout ==================================================

    {
        VkDescriptorSetLayout lightCullingLayouts[3] = { 
            m_DescriptorLayout0, 
            m_DummyDescriptorLayout, 
            m_DescriptorLayout2 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 3,
            .pSetLayouts = lightCullingLayouts,
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_LightCullingPipelineLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyPipelineLayout(device, m_LightCullingPipelineLayout, nullptr);
        });
    }

    // ================================================== Lighting Layout ==================================================

    {
        VkDescriptorSetLayout lightingLayouts[3] = { 
            m_DescriptorLayout0, 
            m_DescriptorLayout1, 
            m_DescriptorLayout2 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 3,
            .pSetLayouts = lightingLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainPushConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_LightingPipelineLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyPipelineLayout(device, m_LightingPipelineLayout, nullptr);
        });
    }

    // ================================================== Bloom Layout ==================================================

    {
        VkPushConstantRange bloomPushConstantRange = {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(BloomPushConstants),
        };

        VkDescriptorSetLayout bloomLayouts[4] = { 
            m_DescriptorLayout0, 
            m_DummyDescriptorLayout, 
            m_DescriptorLayout2,
            m_DescriptorLayout3 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 4,
            .pSetLayouts = bloomLayouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &bloomPushConstantRange
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_BloomPipelineLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyPipelineLayout(device, m_BloomPipelineLayout, nullptr);
        });
    }

    // ================================================== Graphics Pipelines ==================================================

    VertexInputDesc depthVertexDesc = Vertex::GetDepthVertexDesc();
    m_DepthPipeline = CreateGraphicsPipeline({
        .pipelineName = "Depth",
        .vertexShader = "depth.vert",
        .fragmentShader = "depth.frag",
        .viewport = &m_Viewport,
        .scissor = &m_ScissorRect,
        .pipelineLayout = m_MinimalPipelineLayout,
        .vertexInput = &depthVertexDesc,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthFlags = GraphicsPipelineCreateInfo::PIPELINE_DEPTH_TEST 
                | GraphicsPipelineCreateInfo::PIPELINE_DEPTH_WRITE,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthFormat = DEPTH_FORMAT
    });

    m_ShadowPipeline = CreateGraphicsPipeline({
        .pipelineName = "Shadow",
        .vertexShader = "shadow.vert",
        .viewport = &m_ShadowViewport,
        .scissor = &m_ShadowScissorRect,
        .pipelineLayout = m_ShadowPipelineLayout,
        .vertexInput = &depthVertexDesc,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthFlags = GraphicsPipelineCreateInfo::PIPELINE_DEPTH_TEST 
                | GraphicsPipelineCreateInfo::PIPELINE_DEPTH_WRITE,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthFormat = DEPTH_FORMAT
    });

    VertexInputDesc vertexDesc = Vertex::GetVertexDesc();
    m_GBufferPipeline = CreateGraphicsPipeline({
        .pipelineName = "GBuffer",
        .vertexShader = "gBuffer.vert",
        .fragmentShader = "gBuffer.frag",
        .viewport = &m_Viewport,
        .scissor = &m_ScissorRect,
        .pipelineLayout = m_MinimalPipelineLayout,
        .vertexInput = &vertexDesc,
        .depthFlags = GraphicsPipelineCreateInfo::PIPELINE_DEPTH_TEST,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthFormat = DEPTH_FORMAT,
        .attachmentFormats = { ALBEDO_FORMAT, NORMAL_FORMAT, MATERIAL_FORMAT }
    });

    m_TransparencyPipeline = CreateGraphicsPipeline({
        .pipelineName = "Transparency",
        .vertexShader = "transparency.vert",
        .fragmentShader = "transparency.frag",
        .viewport = &m_Viewport,
        .scissor = &m_ScissorRect,
        .pipelineLayout = m_LightingPipelineLayout,
        .vertexInput = &vertexDesc,
        .cullMode = VK_CULL_MODE_NONE,
        .blendEnable = true,
        .depthFlags = GraphicsPipelineCreateInfo::PIPELINE_DEPTH_TEST,
        .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthFormat = DEPTH_FORMAT,
        .attachmentFormats = { LIGHTING_FORMAT }
    });

    m_ToneMapPipeline = CreateGraphicsPipeline({
        .pipelineName = "Tonemapping",
        .vertexShader = "fullscreen.vert", 
        .fragmentShader = "tonemap.frag", 
        .viewport = &m_Viewport, 
        .scissor = &m_ScissorRect, 
        .pipelineLayout = m_MinimalPipelineLayout, 
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthCompare = VK_COMPARE_OP_ALWAYS, 
        .attachmentFormats = { TONE_MAP_FORMAT }
    });

    m_AntiAliasingPipeline = CreateGraphicsPipeline({
        .pipelineName = "Anti-aliasing",
        .vertexShader = "fullscreen.vert", 
        .fragmentShader = "antialias.frag", 
        .viewport = &m_Viewport, 
        .scissor = &m_ScissorRect, 
        .pipelineLayout = m_MinimalPipelineLayout, 
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthCompare = VK_COMPARE_OP_ALWAYS, 
        .attachmentFormats = { m_SwapchainFormat }
    });

    // ================================================== Compute Pipelines ==================================================

    m_LightCullingPipeline = CreateComputePipeline({
        .pipelineName = "Light-Culling",
        .computeShader = "lightCulling.comp",
        .pipelineLayout = m_LightCullingPipelineLayout
    });

    m_LightingPipeline = CreateComputePipeline({
        .pipelineName = "Lighting",
        .computeShader = "lighting.comp",
        .pipelineLayout = m_LightingPipelineLayout
    });

    m_BloomLightExtractionPipeline = CreateComputePipeline({
        .pipelineName = "Bloom-Light-Extraction",
        .computeShader = "lightExtraction.comp",
        .pipelineLayout = m_BloomPipelineLayout
    });

    m_BloomDownsamplePipeline = CreateComputePipeline({
        .pipelineName = "Bloom-Downsample",
        .computeShader = "downsample.comp",
        .pipelineLayout = m_BloomPipelineLayout
    });

    m_BloomHorizontalBlurPipeline = CreateComputePipeline({
        .pipelineName = "Bloom-Horizontal-Blur",
        .computeShader = "horizontalBlur.comp",
        .pipelineLayout = m_BloomPipelineLayout
    });

    m_BloomVerticalBlurPipeline = CreateComputePipeline({
        .pipelineName = "Bloom-Vertical-Blur",
        .computeShader = "verticalBlur.comp",
        .pipelineLayout = m_BloomPipelineLayout
    });

    m_BloomAccumulatePipeline = CreateComputePipeline({
        .pipelineName = "Bloom-Accumulate",
        .computeShader = "accumulate.comp",
        .pipelineLayout = m_BloomPipelineLayout
    });
}

void VulkanBackend::InitMiscBuffers()
{
    // None for now
}

void VulkanBackend::InitMiscDescriptors()
{
    VkDescriptorPoolSize poolSizes[1] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_NORMAL_MIPS * 2 },    // Normal mipmap sources and destinations
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = poolSizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_MiscDescriptorPool));
    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorPool(device, m_MiscDescriptorPool, nullptr);
    });

    // ================================================== Create Normal Mipmap Set  ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[2] = {
            { // Source image 
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = MAX_NORMAL_MIPS,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            { // Destination image 
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = MAX_NORMAL_MIPS,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_NormalMipmapDescriptorLayout));
        m_MainDeleter.push_back([=, this] {
            vkDestroyDescriptorSetLayout(device, m_NormalMipmapDescriptorLayout, nullptr);
        });

        // Allocate
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_MiscDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_NormalMipmapDescriptorLayout
        };
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &m_NormalMipmapDescriptorSet));
    }
}

void VulkanBackend::InitMiscPipelines()
{
    VkPushConstantRange normalMipmapPushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(NormalMipmapPushConstants),
    };

    VkPipelineLayoutCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_NormalMipmapDescriptorLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &normalMipmapPushConstantRange
    };
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_NormalMipmapPipelineLayout));
    m_MainDeleter.push_back([=, this] {
        vkDestroyPipelineLayout(device, m_NormalMipmapPipelineLayout, nullptr);
    });

    m_NormalMipmapPipeline = CreateComputePipeline({
        .pipelineName = "Normal-Mipmap",
        .computeShader = "normalMipmap.comp",
        .pipelineLayout = m_NormalMipmapPipelineLayout
    });
}

VkPipeline VulkanBackend::CreateComputePipeline(const ComputePipelineCreateInfo&& info)
{
    INFO("Creating compute pipeline: " << info.pipelineName);

    fs::path computeShaderFullPath("src/Engine/Resource/Shader" / info.computeShader);
    computeShaderFullPath.replace_extension(".comp.spirv");
    VkShaderModule computeModule = LoadShaderModule(computeShaderFullPath);
    VkComputePipelineCreateInfo lightCullingComputePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = computeModule,
            .pName = "main"
        },
        .layout = info.pipelineLayout
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &lightCullingComputePipelineInfo, nullptr, &pipeline));

    vkDestroyShaderModule(device, computeModule, nullptr);
    m_MainDeleter.push_back([=, this] {
        vkDestroyPipeline(device, pipeline, nullptr);
    });

    return pipeline;
}

VkPipeline VulkanBackend::CreateGraphicsPipeline(const GraphicsPipelineCreateInfo&& info)
{
    INFO("Creating graphics pipeline: " << info.pipelineName);

    bool depthTest = FLAGS_CONTAIN(info.depthFlags, GraphicsPipelineCreateInfo::PIPELINE_DEPTH_TEST);
    bool depthWrite = FLAGS_CONTAIN(info.depthFlags, GraphicsPipelineCreateInfo::PIPELINE_DEPTH_WRITE);
    bool depthBias = FLAGS_CONTAIN(info.depthFlags, GraphicsPipelineCreateInfo::PIPELINE_DEPTH_BIAS);

    uint32_t attachmentCount = info.attachmentFormats.size();
    uint32_t stageCount = (info.fragmentShader.empty()) ? 1 : 2;

    fs::path vertexShaderFullPath("src/Engine/Resource/Shader" / info.vertexShader);
    vertexShaderFullPath.replace_extension(".vert.spirv");
    VkShaderModule vertexModule = LoadShaderModule(vertexShaderFullPath);

    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (stageCount == 2) {
        fs::path fragmentShaderFullPath("src/Engine/Resource/Shader" / info.fragmentShader);
        fragmentShaderFullPath.replace_extension(".frag.spirv");
        fragmentModule = LoadShaderModule(fragmentShaderFullPath);
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = { 
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentModule,
            .pName = "main"
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = (info.vertexInput) ? static_cast<uint32_t>(info.vertexInput->bindings.size()) : 0,
        .pVertexBindingDescriptions = (info.vertexInput) ? info.vertexInput->bindings.data() : nullptr,
        .vertexAttributeDescriptionCount = (info.vertexInput) ? static_cast<uint32_t>(info.vertexInput->attributes.size()) : 0,
        .pVertexAttributeDescriptions = (info.vertexInput) ? info.vertexInput->attributes.data() : nullptr,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo viewportState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = info.viewport,
        .scissorCount = 1,
        .pScissors = info.scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizationState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode = info.cullMode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = (depthBias) ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = (depthBias) ? 1.25f : 0.0f,
        .depthBiasSlopeFactor = (depthBias) ? 1.75f : 0.0f,
        .lineWidth = 1.0
    };

    VkPipelineMultisampleStateCreateInfo multisampleState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo stencilState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = (depthTest) ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (depthWrite) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = info.depthCompare,
        .minDepthBounds = 0.0,
        .maxDepthBounds = 1.0,
    };

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    blendAttachments.reserve(attachmentCount);
    for (uint32_t i = 0; i < attachmentCount; i++) {
        blendAttachments.push_back({
            .blendEnable = (info.blendEnable) ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = (info.blendEnable) ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ZERO,
            .dstColorBlendFactor = (info.blendEnable) ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO,
            .srcAlphaBlendFactor = (info.blendEnable) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT
        });
    };

    VkPipelineColorBlendStateCreateInfo blendState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = attachmentCount,
        .pAttachments = (attachmentCount > 0) ? blendAttachments.data() : nullptr
    };

    std::vector<VkFormat> formatList(info.attachmentFormats);
    VkPipelineRenderingCreateInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = attachmentCount,
        .pColorAttachmentFormats = (attachmentCount > 0) ? formatList.data() : nullptr,
        .depthAttachmentFormat = info.depthFormat,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = stageCount,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &stencilState,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = info.pipelineLayout
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    m_MainDeleter.push_back([=, this] {
        vkDestroyPipeline(device, pipeline, nullptr);
    });

    vkDestroyShaderModule(device, vertexModule, nullptr);
    if (stageCount == 2) {
        vkDestroyShaderModule(device, fragmentModule, nullptr);
    }
    return pipeline;
}

VkShaderModule VulkanBackend::LoadShaderModule(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        FATAL("Failed to open shader file");
    }

    size_t fileSize = file.tellg();
    uint32_t *buf = new uint32_t[fileSize];

    file.seekg(0);
    file.read((char *) buf, fileSize);
    file.close();

    VkShaderModule shaderModule;
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fileSize,
        .pCode = buf,
    };
    VkResult err = vkCreateShaderModule(device, &info, nullptr, &shaderModule);
    ASSERT(!err && "Failed to create shader module");

    delete[] buf;

    return shaderModule;
}

void VulkanBackend::GenerateMips(AllocatedTexture allocation, const ImageResource& image, GraphicsFrontend& frontend)
{
    uint32_t imageWidth = image.size.x;
    uint32_t imageHeight = image.size.y;
    VkDeviceSize imageSizeBytes = imageWidth * imageHeight * image.channels;

    const TextureArray& textureArray = frontend.textureArrays[allocation.arrayID];

    // Staging image
    VkExtent3D extent = {
        .width = imageWidth,
        .height = imageHeight,
        .depth = 1
    };
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureArray.format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (textureArray.textureKind == TextureKind::NORMAL) {
        imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    VmaAllocationCreateInfo gpuAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };

    AllocatedImage stagingImage;
    vmaCreateImage(allocator, &imageInfo, &gpuAllocInfo, &stagingImage.image, &stagingImage.allocation, nullptr);
    VMA_NAME_ALLOCATION(allocator, stagingImage.allocation, "Texture_Array_Staging_Image");

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSizeBytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo cpuAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    vmaCreateBuffer(allocator, &bufferInfo, &cpuAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
    VMA_NAME_ALLOCATION(allocator, stagingBuffer.allocation, "Texture_Array_Staging_Buffer");

    // Copy into staging
    vmaMapMemory(allocator, stagingBuffer.allocation, &stagingBuffer.data);
    std::memcpy(stagingBuffer.data, image.pixels, imageSizeBytes);
    vmaUnmapMemory(allocator, stagingBuffer.allocation);

    VkImageView levelViews[MAX_NORMAL_MIPS] = {}; // In case the texture is a normal map
    submitter.ImmediateSubmit(device, graphicsQueue, [&](VkCommandBuffer commandBuffer) {
        if (textureArray.textureKind == TextureKind::COLOR) { // Colour images get mipmapped with bilinear filtering
            // Transition image to optimal layout for a data transfer
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .image = textureArray.image.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .baseArrayLayer = allocation.layerID,
                    .layerCount = 1
                }
            };
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Transition staging image to optimal too
            barrier.image = stagingImage.image;
            barrier.subresourceRange.baseArrayLayer = 0;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copy buffer to staging image 
            VkBufferImageCopy copy = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
                .imageExtent = { imageWidth, imageHeight, 1 }
            };
            vkCmdCopyBufferToImage(commandBuffer, 
                    stagingBuffer.buffer, stagingImage.image, 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    1, &copy);

            // Transition staging image to transfer source
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Blit staging image to first mip level resizing if required
            VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1
                },
                .srcOffsets = {
                    { 0, 0, 0 },
                    { image.size.x, image.size.y, 1 }
                },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseArrayLayer = allocation.layerID,
                    .layerCount = 1
                },
                .dstOffsets = {
                    { 0, 0, 0 },
                    { static_cast<int32_t>(textureArray.resolution), static_cast<int32_t>(textureArray.resolution), 1 }
                },
            };
            vkCmdBlitImage(commandBuffer, 
                    stagingImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                    textureArray.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    1, &blit, VK_FILTER_LINEAR);

            // Transfer mip level 0 to transfer src
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.image = textureArray.image.image;
            barrier.subresourceRange.baseArrayLayer = allocation.layerID;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Transfer all mip levels except level 0 to transfer destination
            for (uint32_t i = 1; i < textureArray.levelCount; i++) {
                barrier.subresourceRange.baseMipLevel = i;
                barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(commandBuffer, 
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Generate mips
            int32_t mipWidth = textureArray.resolution;
            int32_t mipHeight = textureArray.resolution;
            for (uint32_t i = 1; i < textureArray.levelCount; i++) {
                // Blit next mip level
                VkImageBlit blit = {
                    .srcSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = i - 1,
                        .baseArrayLayer = allocation.layerID,
                        .layerCount = 1
                    },
                    .srcOffsets = {
                        { 0, 0, 0 },
                        { mipWidth, mipHeight, 1 }
                    },
                    .dstSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = i,
                        .baseArrayLayer = allocation.layerID,
                        .layerCount = 1
                    },
                    .dstOffsets = {
                        { 0, 0, 0 },
                        { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
                    },
                };
                vkCmdBlitImage(commandBuffer, 
                        textureArray.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
                        textureArray.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                        1, &blit, VK_FILTER_LINEAR);

                // Wait for blit to finish and transfer to shader-readable
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(commandBuffer, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

                // If not last mip level, transition current level to src, ready to blit to next
                if (i < textureArray.levelCount - 1) {
                    barrier.subresourceRange.baseMipLevel = i;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(commandBuffer, 
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 
                            VK_PIPELINE_STAGE_TRANSFER_BIT, 
                            0, 0, nullptr, 0, nullptr, 1, &barrier);
                }

                // Update dimensions for next level
                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }

            // Transition final mip level to shader-readable
            barrier.subresourceRange.baseMipLevel = textureArray.levelCount - 1;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
        } else if (textureArray.textureKind  == TextureKind::NORMAL) { // Normal images need renormalization when mipmapping
            // Create views for each level (view 0 = staging image view)
            VkImageViewCreateInfo viewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = stagingImage.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = textureArray.format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1
                }
            };
            VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &levelViews[0]));

            viewInfo.image = textureArray.image.image;
            viewInfo.subresourceRange.baseArrayLayer = allocation.layerID;
            for (uint32_t i = 0; i < textureArray.levelCount; i++) {
                viewInfo.subresourceRange.baseMipLevel = i;
                VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &levelViews[i + 1]));
            }

            // Transition image to general
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .image = textureArray.image.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = textureArray.levelCount,
                    .baseArrayLayer = allocation.layerID,
                    .layerCount = 1
                }
            };
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Transition staging image to transfer destination
            barrier.image = stagingImage.image;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.levelCount = 1;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copy buffer to staging image 
            VkBufferImageCopy copy = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
                .imageExtent = { imageWidth, imageHeight, 1 }
            };
            vkCmdCopyBufferToImage(commandBuffer, 
                    stagingBuffer.buffer, stagingImage.image, 
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                    1, &copy);

            // Transition staging image to general
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Update descriptors
            VkDescriptorImageInfo imageInfos[MAX_NORMAL_MIPS] = {};
            for (uint32_t i = 0; i < textureArray.levelCount + 1; i++) {
                imageInfos[i] = {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = levelViews[i],
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                };
            }
            // Point all remaining descriptors to the last mip to avoid validation issues
            for (uint32_t i = textureArray.levelCount + 1; i < MAX_NORMAL_MIPS; i++) {
                imageInfos[i] = {
                    .sampler = VK_NULL_HANDLE,
                    .imageView = levelViews[textureArray.levelCount],
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL
                };
            }

            VkWriteDescriptorSet writes[2] = {
                { // Source
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_NormalMipmapDescriptorSet,
                    .dstBinding = 0,
                    .descriptorCount = MAX_NORMAL_MIPS,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = imageInfos
                },
                { // Destination
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_NormalMipmapDescriptorSet,
                    .dstBinding = 1,
                    .descriptorCount = MAX_NORMAL_MIPS,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = imageInfos
                }
            };
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

            // Prepare to compute
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_NormalMipmapPipeline);
            vkCmdBindDescriptorSets(commandBuffer, 
                    VK_PIPELINE_BIND_POINT_COMPUTE, m_NormalMipmapPipelineLayout, 
                    0, 1, &m_NormalMipmapDescriptorSet, 0, nullptr);

            // Scale up staging image to top mip level
            NormalMipmapPushConstants constants = {
                .srcIndex = 0,
                .srcWidth = imageWidth,
                .srcHeight = imageHeight,
                .dstIndex = 1,
                .dstWidth = textureArray.resolution,
                .dstHeight = textureArray.resolution
            };
            vkCmdPushConstants(commandBuffer, 
                    m_NormalMipmapPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                    0, sizeof(NormalMipmapPushConstants), 
                    &constants);

            uint32_t groupsX = (textureArray.resolution + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
            uint32_t groupsY = (textureArray.resolution + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
            vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.image = textureArray.image.image;
            barrier.subresourceRange.baseArrayLayer = allocation.layerID;
            barrier.subresourceRange.levelCount = 1;

            for (uint32_t i = 0; i < textureArray.levelCount - 1; i++) {
                uint32_t srcLevel = i;
                uint32_t dstLevel = i + 1;

                uint32_t srcSize = glm::max(textureArray.resolution >> srcLevel, 1u);
                uint32_t dstSize = glm::max(textureArray.resolution >> dstLevel, 1u);

                // NOTE: Index 0 of the bound view is the staging image so the mips start at index 1
                NormalMipmapPushConstants constants = {
                    .srcIndex = srcLevel + 1,
                    .srcWidth = srcSize,
                    .srcHeight = srcSize,
                    .dstIndex = dstLevel + 1,
                    .dstWidth = dstSize,
                    .dstHeight = dstSize
                };

                vkCmdPushConstants(commandBuffer, 
                        m_NormalMipmapPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                        0, sizeof(NormalMipmapPushConstants), 
                        &constants);

                uint32_t groupsXY = (dstSize + COMPUTE_TILE_SIZE - 1) / COMPUTE_TILE_SIZE;
                vkCmdDispatch(commandBuffer, groupsXY, groupsXY, 1);

                // Wait for current mip to finish rendering
                barrier.subresourceRange.baseMipLevel = srcLevel;
                vkCmdPipelineBarrier(commandBuffer, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Transition all levels to shader readable
            barrier.image = textureArray.image.image;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = textureArray.levelCount;
            barrier.subresourceRange.baseArrayLayer = allocation.layerID;
            barrier.subresourceRange.baseMipLevel = 0;
            vkCmdPipelineBarrier(commandBuffer, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

        } else {
            FATAL("Bad texture kind for texture array"); 
        }
    });

    // Normal mipmapping done by compute shader so views were made for the levels
    if (textureArray.textureKind == TextureKind::NORMAL) {
        for (uint32_t i = 0; i < textureArray.levelCount + 1; i++) {
            vkDestroyImageView(device, levelViews[i], nullptr);
        }
    }

    vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    vmaDestroyImage(allocator, stagingImage.image, stagingImage.allocation);
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
