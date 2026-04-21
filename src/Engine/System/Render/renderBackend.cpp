#include "renderBackend.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "Component/light.h"
#include "Component/camera.h"
#include "Component/shadowcaster.h"
#include "ECS/ecs.h"
#include "ECS/ecsTypes.h"
#include "Geometry/frustum.h"
#include "System/Render/initialiser.h"
#include "System/Render/pipeline.h"
#include "System/Render/renderTypes.h"
#include "System/Render/textureArrayHandler.h"
#include "System/Render/writableTextureArray.h"
#include "Util/allocator.h"
#include "Util/logger.h"
#include "Util/imageLoader.h"
#include "Component/transform.h"
#include "Component/material.h"
#include "Util/profiler.h"

#include <VkBootstrap/VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <random>
#include <vulkan/vulkan_core.h>
#include <stb/stb_image.h>

// TODO: Subpasses 
//       - Should optimise some of the horrible transitions I am doing 
//       - Perhaps the depth pass can be a subpass?

void RenderBackend::Init(GLFWwindow *window, Config& config)
{
    m_PresentMode = config.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    m_DrawOrder.reserve(MAX_ENTITIES);

    // Viewport and scissor rect for on-screen passes
    m_Extent.width = config.windowWidth;
    m_Extent.height = config.windowHeight;
    m_Viewport = {
        .width = static_cast<float>(m_Extent.width),
        .height = static_cast<float>(m_Extent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };
    m_Scissor = {
        .extent = m_Extent
    };

    // Viewport and scissor rect for offscreen shadowmap passes
    m_ShadowExtent.width = SHADOWMAP_RESOLUTION;
    m_ShadowExtent.height = SHADOWMAP_RESOLUTION;
    m_ShadowViewport = {
        .width = static_cast<float>(m_ShadowExtent.width),
        .height = static_cast<float>(m_ShadowExtent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };
    m_ShadowScissor = {
        .offset = { 0, 0 },
        .extent = m_ShadowExtent
    };

    // Primitives and vulkan core structures
    InitVulkan(window);
    InitMSAA(config.msaaSamples);
    InitSwapchain();
    InitCommands();
    InitSyncStructures();
    // Render passes
    InitMainRenderPass();
    InitDepthRenderPass();
    InitShadowRenderPass();
    // Descriptor sets
    InitGlobalDescriptor();
    InitResolveDescriptor();
    InitLightCullingDescriptor();
    // Dynamic Resources 
    InitResolveResources();
    InitLightCullingResources();
    InitShadowResources();
    InitSSAOResources();
    // Pipelines
    InitMainPipelines();
    InitDepthPipeline();
    InitResolvePipeline();
    InitLightCullingPipeline();
    InitShadowPipeline();
    // Framebuffers
    InitFramebuffers();

    // Shadowmaps are stored in a texture array but are managed differently to these arrays
    // since these arrays are readonly, and the shadowmap array is writable
    TextureArraySizes arraySizes = {
        .color1024 = 128,   // 1024*1024*128*4 = 512 MiB
        .color2048 = 32,    // 2048*2048*32*4  = 512 MiB
        .data1024 = 128,    // 1024*1024*128*4 = 512 MiB
        .data2048 = 32,     // 2048*2048*32*4  = 512 MiB
    };
    m_ArrayHandler.Init(m_Device, m_PhysicalDevice, m_GraphicsQueue, m_Submitter, m_Allocator, m_Frames, arraySizes);
    m_MainDeletionQueue.PushFunction([=, this] {
        m_ArrayHandler.Cleanup(m_Device, m_Allocator);
    });

    // Set up gpu profiling with vulkan timestamps
#ifdef PROFILING 
    std::string headings[6] = { "Depth", "Resolve", "Culling", "Shadow", "Opaque", "Translucent", "Total" };
    PROFILER_BEGIN_GPU_SESSION(headings, 7);

    if (m_Properties.limits.timestampPeriod == 0 || !m_Properties.limits.timestampComputeAndGraphics) {
        ERROR("Physical device doesn't support timestamps");

        uint32_t propertyCount = 1;
        VkQueueFamilyProperties graphicsQueueProperties;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &propertyCount, &graphicsQueueProperties);
        if (graphicsQueueProperties.timestampValidBits == 0) {
            ERROR("Graphics queue doesn't support timestamps");
        }

        abort();
    }

    for (FrameData& frame : m_Frames) {
        frame.queryCount = 10;

        VkQueryPoolCreateInfo queryPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = frame.queryCount,
        };
        vkCreateQueryPool(m_Device, &queryPoolInfo, nullptr, &frame.queryPool);

        frame.queryTimestamps.resize(frame.queryCount);

        m_Submitter.ImmediateSubmit(m_Device, m_GraphicsQueue, [=](VkCommandBuffer commandBuffer) {
            vkCmdResetQueryPool(commandBuffer, frame.queryPool, 0, frame.queryTimestamps.size());
        });

        m_MainDeletionQueue.PushFunction([=, this] {
            vkDestroyQueryPool(m_Device, frame.queryPool, nullptr);
        });
    }
#endif // PROFILING
}

void RenderBackend::Cleanup() 
{
    VK_CHECK(vkDeviceWaitIdle(m_Device));

    m_MainDeletionQueue.Flush();
    m_DynamicDeletionQueue.Flush();

    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
    vkDestroyInstance(m_Instance, nullptr);
}

void RenderBackend::Draw(Entity camera, std::set<Entity>& entities)
{
    PROFILER_PROFILE_SCOPE("RenderBackend::Update");

    if (m_ResizeCamera != INVALID_HANDLE) {
        Resize(m_ResizeCamera);
    }

    ECS& ecs = ECS::Get();

    Camera& cam = ecs.GetComponent<Camera>(camera);
    Transform& camTransform = ecs.GetComponent<Transform>(camera);
    glm::vec3 camTranslation = camTransform.translation;
    if (camTransform.inherit != INVALID_HANDLE) {
        camTranslation += ecs.GetComponent<Transform>(camTransform.inherit).translation;
    }
    glm::mat4x4 camVP = cam.projection * cam.view;
    Frustum camFrustum = Frustum(camVP);

    // Split up opaque and translucent entities, and perform frustum culling 
    int32_t entityCount = entities.size();
    int32_t frontPtr = 0;
    int32_t backPtr = entityCount - 1;

    { PROFILER_PROFILE_SCOPE("Sort_Entities");

    for (const Entity e : entities) {
        // Frustum culling
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        glm::mat4x4 model = transform.GlobalModelMatrix();
        CentreExtents centreExtents = CentreExtents(mesh.bounds);
        if (camFrustum.Intersects(centreExtents.WorldSpace(model))) {
            // Add to draw list (front for opaque, back for transparent)
            if (ecs.GetComponent<Material>(e).hasTransparency) {
                m_DrawOrder[backPtr--] = e;
            } else {
                m_DrawOrder[frontPtr++] = e;
            }
        } else {
            // Keep the entity vector packed while skipping one
            if (backPtr < entityCount - 1) {
                m_DrawOrder[backPtr] = m_DrawOrder[entityCount - 1];
            }
            backPtr--;
            entityCount--;
        }
    }

    } // PROFILER_PROFILE_SCOPE("Sort_Entities");

    VkClearValue colorClear = { .color = {{ 0.5, 0.7, 1.0, 1.0 }} };
    VkClearValue resolveClear = { .color = {{ 0.0, 0.0, 0.0, 0.0 }} };
    VkClearValue depthClear = { .depthStencil = { .depth = 1.0f } };
    VkClearValue clearValues[3] = { colorClear, resolveClear, depthClear };

    // ============== Acquire swapchain image and wait on fences ===============

    uint32_t imageIndex;
    size_t frameIndex = m_FrameNum % FRAMES_IN_FLIGHT;
    FrameData &frame = m_Frames[frameIndex];

    { PROFILER_PROFILE_SCOPE("Acquire_Swapchain_Image");

    VK_CHECK(vkWaitForFences(m_Device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_Device, 1, &frame.renderFence));

    VkResult acquireErr = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, frame.acquireSemaphore, nullptr, &imageIndex);
    if (acquireErr == VK_ERROR_OUT_OF_DATE_KHR || acquireErr == VK_SUBOPTIMAL_KHR) {
        m_ResizeCamera = camera;
        return;
    } else if (acquireErr == VK_NOT_READY) {
        return;
    } 
    VK_CHECK(acquireErr);

    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    } // PROFILER_PROFILE_SCOPE("Acquire_Swapchain_Image");

    SwapchainImageData &image = m_Images[imageIndex];

    { PROFILER_PROFILE_SCOPE("Wait_For_Image_Fences");
    if (image.flightFence != VK_NULL_HANDLE && image.flightFence != frame.renderFence) {
        VK_CHECK(vkWaitForFences(m_Device, 1, &image.flightFence, VK_TRUE, UINT64_MAX));
    }
    image.flightFence = frame.renderFence;

    } // PROFILER_PROFILE_SCOPE("Wait_For_Image_Fences");

    // ======================= Begin recording commands ========================

    { PROFILER_PROFILE_SCOPE("Begin_Command_Buffer");

    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

#ifdef PROFILING 
    vkCmdResetQueryPool(frame.commandBuffer, frame.queryPool, 0, frame.queryTimestamps.size());
#endif

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_Scissor);

    } // PROFILER_PROFILE_SCOPE("Begin_Command_Buffer");

    // ============================ Depth pre-pass =============================

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, 0);
#endif

    VkRenderPassBeginInfo depthPassInfo = vkinit::RenderPassBeginInfo(m_DepthRenderPass, image.depthFramebuffer, &depthClear, 1, m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &depthPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthPipeline);

    // Only draw opaque entities to depth buffer
    for (int32_t i = 0; i < frontPtr; i++) {
        const Entity e = m_DrawOrder[i];

        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        glm::mat4x4 model = transform.GlobalModelMatrix();

        DepthPushConstants constants = {
            .model = model,
            .vp = camVP
        };
        vkCmdPushConstants(frame.commandBuffer, m_DepthPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DepthPushConstants), &constants);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(frame.commandBuffer);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, 1);
#endif

    // ========================= Depth Resolve (MSAA) ==========================

    // Transition resolved image to general for resolve
    VkImageMemoryBarrier barrierToGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = image.resolvedDepthImage.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToGeneral);

    VkImageMemoryBarrier barrierToShader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = image.depthImage.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

    VkDescriptorImageInfo srcImageInfo = {
        .sampler = m_DepthSampler,
        .imageView = image.depthView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo dstImageInfo = {
        .sampler = VK_NULL_HANDLE,
        .imageView = image.resolvedDepthView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = frame.resolveDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &srcImageInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = frame.resolveDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &dstImageInfo,
        },
    };
    vkUpdateDescriptorSets(m_Device, 2, writes, 0, nullptr);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, frame.queryPool, 2);
#endif

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipeline);

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipelineLayout, 0, 1, &frame.resolveDescriptorSet, 0, nullptr);

    const uint32_t GROUP_SIZE = 8;
    uint32_t groupsX = (m_Extent.width + GROUP_SIZE - 1) / GROUP_SIZE;
    uint32_t groupsY = (m_Extent.height + GROUP_SIZE - 1) / GROUP_SIZE;

    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, frame.queryPool, 3);
#endif

    // Transition to readable by light culling
    VkImageMemoryBarrier barrierToReadable = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = image.resolvedDepthImage.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToReadable);

    // Transition to readable by main shader
    VkImageMemoryBarrier barrierToAttachment = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .image = image.depthImage.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToAttachment);

    // ========================== Light Culling Pass ===========================

    // Copy lights into the input buffer of the compute shader
    const auto [lightCount, lights] = ecs.GetData<Light>();
    ASSERT(lightCount < MAX_LIGHTS && "Light buffer overflow");
    memcpy(frame.lightCulling.lightBuffer.data, lights, lightCount * sizeof(Light));

    // Zero compute outputs
    const uint32_t c = m_TilesX * m_TilesY * sizeof(uint32_t);
    vkCmdFillBuffer(frame.commandBuffer, frame.lightCulling.lightIndices.buffer, 0, MAX_TILE_LIGHTS * c, 0);
    vkCmdFillBuffer(frame.commandBuffer, frame.lightCulling.tileCounts.buffer, 0, c, 0);

    VkBufferMemoryBarrier fillBarriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = frame.lightCulling.lightIndices.buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = frame.lightCulling.tileCounts.buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        },
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, fillBarriers, 0, nullptr);

    LightCullingPushConstants lightCullingConstants {
        .tileSize = TILE_SIZE,
        .tilesX = m_TilesX,
        .tilesY = m_TilesY,
        .lightCount = lightCount,
        .maxTileLights = MAX_TILE_LIGHTS,
        .near = cam.near,
        .far = cam.far,
        .screenWidth = m_Extent.width,
        .screenHeight = m_Extent.height
    };
    vkCmdPushConstants(frame.commandBuffer, m_LightCullingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LightCullingPushConstants), &lightCullingConstants);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, frame.queryPool, 4);
#endif

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightCullingPipeline);

    // Set camera uniform data
    CameraUniforms cameraUniforms = {
        .view = cam.view,
        .projection = cam.projection,
        .viewPos = camTranslation
    };
    memcpy(frame.lightCulling.uniformBuffer.data, &cameraUniforms, sizeof(CameraUniforms));

    VkDescriptorImageInfo depthInfo = {
        .sampler = m_DepthSampler,
        .imageView = image.resolvedDepthView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet depthWrite = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = frame.lightCulling.descriptorSet,
        .dstBinding = 4,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &depthInfo,
    };
    vkUpdateDescriptorSets(m_Device, 1, &depthWrite, 0, nullptr);
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightCullingPipelineLayout, 0, 1, &frame.lightCulling.descriptorSet, 0, nullptr);

    vkCmdDispatch(frame.commandBuffer, m_TilesX, m_TilesY, 1);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, frame.queryPool, 5);
#endif

    VkMemoryBarrier memoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    // ========================== Shadowcaster pass ============================

    frame.shadowArray.TransitionFromReadableToAttachment(frame.commandBuffer);

    const auto [shadowcasterCount, shadowcasters] = ecs.GetData<Shadowcaster>();

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_ShadowViewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_ShadowScissor);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, 6);
#endif

    for (uint32_t i = 0; i < shadowcasterCount; i++) {
        Shadowcaster shadowcaster = shadowcasters[i];
        ASSERT(shadowcaster.shadowIndex < MAX_SHADOWCASTERS && "Rendering shadowmap for unallocated shadowcaster");

        VkFramebuffer framebuffer = frame.shadowArray.GetLayerFramebuffer(shadowcaster.shadowIndex);
        VkRenderPassBeginInfo shadowPassInfo = vkinit::RenderPassBeginInfo(m_ShadowRenderPass, framebuffer, &depthClear, 1, m_ShadowExtent);
        vkCmdBeginRenderPass(frame.commandBuffer, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);

        // Only draw opaque entities to depth buffer from the light's perspective
        // TODO: Frustum culling on the light's matrix
        for (const Entity e : entities) {
            if (ecs.GetComponent<Material>(e).hasTransparency) continue;

            Mesh& mesh = ecs.GetComponent<Mesh>(e);
            Transform& transform = ecs.GetComponent<Transform>(e);
            glm::mat4x4 model = transform.GlobalModelMatrix();

            DepthPushConstants constants = {
                .model = model,
                .vp = shadowcaster.projection * shadowcaster.view
            };
            vkCmdPushConstants(frame.commandBuffer, m_ShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DepthPushConstants), &constants);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
            vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
        }

        vkCmdEndRenderPass(frame.commandBuffer);
    }

    // Transition any shadowcaster layers that weren't used to shader read only
    frame.shadowArray.TransitionRemainingLayersToReadable(frame.commandBuffer);

    // Write descriptor set for whole shadowmap array
    VkDescriptorImageInfo imageInfo = {
        .sampler = frame.shadowArray.GetSampler(),
        .imageView = frame.shadowArray.GetView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = frame.textureDescriptorSet,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, 7);
#endif

    // ========================= Opaque forward+ pass ==========================

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, 8);
#endif

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_Scissor);

    VkRenderPassBeginInfo passInfo = vkinit::RenderPassBeginInfo(m_RenderPass, image.framebuffer, &clearValues[0], 3, m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set uniform vp matrix
    memcpy(static_cast<char *>(frame.uniformBuffer.data) + offsetof(GlobalUniforms, vp), &camVP, sizeof(glm::mat4x4));

    VkDescriptorSet sets[3] = { frame.descriptorSet, frame.textureDescriptorSet, frame.lightCulling.descriptorSet };
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 3, sets, 0, nullptr);

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    for (int32_t i = 0; i < frontPtr; i++) {
        const Entity e = m_DrawOrder[i];

        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);

        // Apply local transform and any inherited transform
        glm::mat4x4 model = transform.GlobalModelMatrix();

        ASSERT(mesh.allocated && "Drawing unallocated mesh");

        Material& material = ecs.GetComponent<Material>(e);

        PushConstants constants = {
            .model              = model,
            .specularExponent   = material.specularExponent,
            .ambientIndex       = (material.ambientTexture.arrayID << 16) | material.ambientTexture.textureID,
            .diffuseIndex       = (material.diffuseTexture.arrayID << 16) | material.diffuseTexture.textureID,
            .normalIndex        = (material.normalTexture.arrayID << 16) | material.normalTexture.textureID,
            .tileSize           = TILE_SIZE,
            .tilesX             = m_TilesX,
            .tilesY             = m_TilesY,
            .maxTileLights      = MAX_TILE_LIGHTS,
            .screenWidth        = m_Extent.width,
            .screenHeight       = m_Extent.height
        };
        vkCmdPushConstants(frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
    }

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, 9);
#endif

    // ======================= Translucent forward+ pass =======================

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queryPool, 10);
#endif

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TransparencyPipeline);
    for (int32_t i = frontPtr; i < entityCount; i++) {
        const Entity e = m_DrawOrder[i];

        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        glm::mat4x4 model = transform.GlobalModelMatrix();

        ASSERT(mesh.allocated && "Drawing unallocated mesh");

        Material& material = ecs.GetComponent<Material>(e);

        PushConstants constants = {
            .model              = model,
            .specularExponent   = material.specularExponent,
            .ambientIndex       = (material.ambientTexture.arrayID << 16) | material.ambientTexture.textureID,
            .diffuseIndex       = (material.diffuseTexture.arrayID << 16) | material.diffuseTexture.textureID,
            .normalIndex        = (material.normalTexture.arrayID << 16) | material.normalTexture.textureID,
            .tileSize           = TILE_SIZE,
            .tilesX             = m_TilesX,
            .tilesY             = m_TilesY,
            .maxTileLights      = MAX_TILE_LIGHTS,
            .screenWidth        = m_Extent.width,
            .screenHeight       = m_Extent.height
        };
        vkCmdPushConstants(frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(frame.commandBuffer);

#ifdef PROFILING
    vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queryPool, 11);
#endif

    // Transition resolved depth image back to general for submit
    barrierToGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToGeneral);

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

#ifdef PROFILING 
    vkGetQueryPoolResults(m_Device, frame.queryPool, 0, frame.queryTimestamps.size(), frame.queryTimestamps.size() * sizeof(uint64_t), frame.queryTimestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    float periodNS = m_Properties.limits.timestampPeriod;

    float depthNS       = (frame.queryTimestamps[1] - frame.queryTimestamps[0]) * periodNS;
    float resolveNS     = (frame.queryTimestamps[3] - frame.queryTimestamps[2]) * periodNS;
    float cullingNS     = (frame.queryTimestamps[5] - frame.queryTimestamps[4]) * periodNS;
    float shadowNS      = (frame.queryTimestamps[7] - frame.queryTimestamps[6]) * periodNS;
    float opaqueNS      = (frame.queryTimestamps[9] - frame.queryTimestamps[8]) * periodNS;
    float translucentNS = (frame.queryTimestamps[11] - frame.queryTimestamps[10]) * periodNS;
    float gpuTimeNS     = (frame.queryTimestamps[11] - frame.queryTimestamps[0]) * periodNS;

    float gpuValues[6] = { depthNS, resolveNS, cullingNS, shadowNS, opaqueNS, translucentNS, gpuTimeNS };
    PROFILER_GPU_VALUES(gpuValues);
#endif

    // ========================== Submit and Present ===========================

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = vkinit::SubmitInfo(&frame, &image, &waitStage);
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, frame.renderFence));

    VkPresentInfoKHR presentInfo = vkinit::PresentInfo(&image, &m_Swapchain, &imageIndex);
    VkResult presentErr = vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
    if (presentErr == VK_ERROR_OUT_OF_DATE_KHR || presentErr == VK_SUBOPTIMAL_KHR) {
        m_ResizeCamera = camera;
    } else {
        VK_CHECK(presentErr);
    }

    m_FrameNum++;
}

void RenderBackend::RequestResize(Entity camera)
{
    m_ResizeCamera = camera;
}

void RenderBackend::AllocateMesh(Mesh& mesh)
{
    const size_t bufferSize = mesh.vertexCount * sizeof(Vertex);

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT // Only for data transfer, not rendering
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    VK_CHECK(vmaCreateBuffer(m_Allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

    vmaMapMemory(m_Allocator, stagingBuffer.allocation, &stagingBuffer.data);
    memcpy(stagingBuffer.data, mesh.vertices, bufferSize);
    vmaUnmapMemory(m_Allocator, stagingBuffer.allocation);

    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = mesh.vertexCount * sizeof(Vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT // Data transfer destination
    };

    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // Target buffer is only for the gpu (faster)
    VK_CHECK(vmaCreateBuffer(m_Allocator, &vertexBufferInfo, &allocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr));

    // Copy the buffer to the GPU
    m_Submitter.ImmediateSubmit(m_Device, m_GraphicsQueue, [=](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = bufferSize,
        };
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
    });

    // Clean up mesh and staging buffer
    delete[] mesh.vertices;
    vmaDestroyBuffer(m_Allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    m_MainDeletionQueue.PushFunction([=, this] {
        vmaDestroyBuffer(m_Allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
    });

    mesh.allocated = true;
}

void RenderBackend::AllocateMaterialTextures(const MaterialTextureInfo& info, Material& mat)
{
    mat.ambientTexture = (info.ambientTextureData) 
                       ? m_ArrayHandler.AllocateTexture(m_Device, m_PhysicalDevice, m_GraphicsQueue, m_Submitter, m_Allocator, info.ambientTextureData)
                       : m_ArrayHandler.GetFallbackTexture(info.ambientTextureData, IMAGE_KIND_COLOR);
    mat.diffuseTexture = (info.diffuseTextureData) 
                       ? m_ArrayHandler.AllocateTexture(m_Device, m_PhysicalDevice, m_GraphicsQueue, m_Submitter, m_Allocator, info.diffuseTextureData)
                       : m_ArrayHandler.GetFallbackTexture(info.diffuseTextureData, IMAGE_KIND_COLOR);
    mat.normalTexture = (info.normalTextureData) 
                      ? m_ArrayHandler.AllocateTexture(m_Device, m_PhysicalDevice, m_GraphicsQueue, m_Submitter, m_Allocator, info.normalTextureData)
                      : m_ArrayHandler.GetFallbackTexture(info.normalTextureData, IMAGE_KIND_NORMAL);
}

void RenderBackend::AllocateShadowcaster(Light& light, Shadowcaster& shadowcaster)
{
    // All frames must have the same index, so we allocate from each and test equality
    uint32_t index = UINT32_MAX;
    for (FrameData& frame : m_Frames) {
        uint32_t frameIndex = frame.shadowArray.Allocate();
        ASSERT(index == UINT32_MAX || index == frameIndex && "Shadowmap index is different across frames");
        index = frameIndex;
    }
    light.shadowIndex = index; 
    shadowcaster.shadowIndex = index;
}

void RenderBackend::Resize(Entity camera)
{
    PROFILER_PROFILE_SCOPE("RenderBackend::Resize");

    ECS& ecs = ECS::Get();

    // Check if the resize is valid
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(m_GetSurfaceCapabilities(m_PhysicalDevice, m_Surface, &capabilities));

    uint32_t w = capabilities.currentExtent.width;
    uint32_t h = capabilities.currentExtent.height;
    if (w == 0 || h == 0 || w == UINT32_MAX || h == UINT32_MAX || (w == m_Extent.width && h == m_Extent.height)) {
        return;
    }

    // Recreate all required structures
    VK_CHECK(vkDeviceWaitIdle(m_Device));

    for (FrameData& frame : m_Frames) {
        VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    }

    m_DynamicDeletionQueue.Flush();

    for (SwapchainImageData& image : m_Images) {
        image.flightFence = VK_NULL_HANDLE;
    }

    m_Extent = capabilities.currentExtent;
    m_Viewport.width = static_cast<float>(m_Extent.width);
    m_Viewport.height = static_cast<float>(m_Extent.height);
    m_Scissor.extent = m_Extent;

    InitSwapchain();
    InitResolveResources();
    InitLightCullingResources();
    InitFramebuffers();
    InitSyncStructures();

    Camera& cam = ecs.GetComponent<Camera>(camera);
    cam.aspect = static_cast<float>(m_Extent.width) / static_cast<float>(m_Extent.height);
    cam.projection = Camera::VulkanPerspective(cam.fov, cam.aspect, cam.near, cam.far);

    m_ResizeCamera = INVALID_HANDLE;
}

void RenderBackend::InitVulkan(GLFWwindow *window)
{
    vkb::InstanceBuilder builder;
    auto builtInstance = builder.set_app_name("Renderer")
                                .request_validation_layers(USE_VALIDATION_LAYERS)
                                .require_api_version(1, 1, 0)
                                .use_default_debug_messenger()
                                .build();

    vkb::Instance vkbInstance = builtInstance.value();

    m_Instance = vkbInstance.instance;
    m_DebugMessenger = vkbInstance.debug_messenger;

    VK_CHECK(glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface));

    vkb::PhysicalDeviceSelector selector { vkbInstance };
    std::vector<std::string> devices = selector.set_surface(m_Surface)
                                               .select_device_names()
                                               .value();

    VERIFY(devices.size() > 0 && "Failed to find a physical device");
    if (devices.size() == 1) { 
        WARN("Only available physical device is: " << devices.front());
    }

    VkPhysicalDeviceFeatures features = {
        .sampleRateShading = VK_TRUE
    };
    vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1)
                                                 .set_surface(m_Surface)
                                                 .set_required_features(features)
                                                 .select()
                                                 .value();
    vkb::DeviceBuilder deviceBuilder { physicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    m_Device = vkbDevice.device;
    m_PhysicalDevice = vkbDevice.physical_device;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);

    m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    INFO("Selected physical device: " << physicalDevice.name);

    VmaAllocatorCreateInfo allocInfo = {
        .physicalDevice = m_PhysicalDevice,
        .device = m_Device,
        .instance = m_Instance,
    };
    vmaCreateAllocator(&allocInfo, &m_Allocator);

    m_GetSurfaceCapabilities = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
}

void RenderBackend::InitMSAA(uint64_t requestedSamples)
{
    VkSampleCountFlags available = m_Properties.limits.framebufferColorSampleCounts & m_Properties.limits.framebufferDepthSampleCounts;

    if (available & VK_SAMPLE_COUNT_64_BIT) m_MSAASamples = VK_SAMPLE_COUNT_64_BIT;
    else if (available & VK_SAMPLE_COUNT_32_BIT) m_MSAASamples = VK_SAMPLE_COUNT_32_BIT;
    else if (available & VK_SAMPLE_COUNT_16_BIT) m_MSAASamples = VK_SAMPLE_COUNT_16_BIT;
    else if (available & VK_SAMPLE_COUNT_8_BIT) m_MSAASamples = VK_SAMPLE_COUNT_8_BIT;
    else if (available & VK_SAMPLE_COUNT_4_BIT) m_MSAASamples = VK_SAMPLE_COUNT_4_BIT;
    else if (available & VK_SAMPLE_COUNT_2_BIT) m_MSAASamples = VK_SAMPLE_COUNT_2_BIT;
    else m_MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    INFO("Maximum MSAA samples: " << m_MSAASamples);
    // Force enable MSAA so transparency works
    if (m_MSAASamples > std::min(requestedSamples, 2ul)) {
        m_MSAASamples = static_cast<VkSampleCountFlagBits>(requestedSamples);
    }
    INFO("Using MSAA samples: " << m_MSAASamples);
}

void RenderBackend::InitSwapchain()
{
    vkb::SwapchainBuilder builder { m_PhysicalDevice, m_Device, m_Surface };
    vkb::Swapchain vkbSwapchain = builder.use_default_format_selection()
                                         .set_desired_present_mode(m_PresentMode)
                                         .set_desired_min_image_count(3)
                                         .set_desired_extent(m_Extent.width, m_Extent.height)
                                         .build()
                                         .value();

    if (vkbSwapchain.present_mode != m_PresentMode) {
        WARN("Requested present mode unavailable: " << m_PresentMode << ". Fell back to: " << vkbSwapchain.present_mode);
    }

    m_Swapchain = vkbSwapchain.swapchain;
    m_SwapchainFormat = vkbSwapchain.image_format;

    std::vector<VkImage> swapchainImages = vkbSwapchain.get_images().value();
    std::vector<VkImageView> swapchainViews = vkbSwapchain.get_image_views().value();
    m_Images = std::vector<SwapchainImageData>(swapchainImages.size());
    for (size_t i = 0; i < m_Images.size(); i++) {
        m_Images[i].image = swapchainImages[i];
        m_Images[i].view = swapchainViews[i];

        m_DynamicDeletionQueue.PushFunction([=, this] { 
            vkDestroyImageView(m_Device, m_Images[i].view, nullptr); 
        });
    }

    INFO("Using swapchain images: " << m_Images.size());

    m_DynamicDeletionQueue.PushFunction([=, this] { 
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr); 
    });

    VkExtent3D depthExtent = {
        .width = m_Extent.width,
        .height = m_Extent.height,
        .depth = 1
    };

    VkImageCreateInfo depthImageInfo = vkinit::ImageCreateInfo(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, depthExtent);
    depthImageInfo.samples = m_MSAASamples;
    VmaAllocationCreateInfo depthImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    for (SwapchainImageData& image : m_Images) {
        vmaCreateImage(m_Allocator, &depthImageInfo, &depthImageAllocInfo, &image.depthImage.image, &image.depthImage.allocation, nullptr);

        VkImageViewCreateInfo depthViewInfo = vkinit::ImageViewCreateInfo(VK_FORMAT_D32_SFLOAT, image.depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK(vkCreateImageView(m_Device, &depthViewInfo, nullptr, &image.depthView));

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vkDestroyImageView(m_Device, image.depthView, nullptr);
            vmaDestroyImage(m_Allocator, image.depthImage.image, image.depthImage.allocation);
        });
    }

    VkExtent3D msaaExtent = {
        .width = m_Extent.width,
        .height = m_Extent.height,
        .depth = 1
    };

    VkImageCreateInfo msaaImageInfo = vkinit::ImageCreateInfo(m_SwapchainFormat, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, msaaExtent);
    msaaImageInfo.samples = m_MSAASamples;
    VmaAllocationCreateInfo msaaImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    };
    for (SwapchainImageData& image : m_Images) {
        vmaCreateImage(m_Allocator, &msaaImageInfo, &msaaImageAllocInfo, &image.msaaColorImage.image, &image.msaaColorImage.allocation, nullptr);

        VkImageViewCreateInfo msaaViewInfo = vkinit::ImageViewCreateInfo(m_SwapchainFormat, image.msaaColorImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(m_Device, &msaaViewInfo, nullptr, &image.msaaColorView));

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vkDestroyImageView(m_Device, image.msaaColorView, nullptr);
            vmaDestroyImage(m_Allocator, image.msaaColorImage.image, image.msaaColorImage.allocation);
        });
    }
}

void RenderBackend::InitCommands()
{
    VkCommandPoolCreateInfo createInfo = vkinit::CommandPoolCreateInfo(m_GraphicsQueueFamily);

    for (FrameData& frame : m_Frames) {
        VK_CHECK(vkCreateCommandPool(m_Device, &createInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo alloc_info = vkinit::CommandBufferAllocateInfo(frame.commandPool);
        VK_CHECK(vkAllocateCommandBuffers(m_Device, &alloc_info, &frame.commandBuffer));

        m_MainDeletionQueue.PushFunction([=, this] { 
            vkDestroyCommandPool(m_Device, frame.commandPool, nullptr); 
        });
    }

    m_Submitter.Init(m_Device, m_GraphicsQueueFamily);
    m_MainDeletionQueue.PushFunction([=, this] {
        m_Submitter.Cleanup(m_Device);
    });
}

void RenderBackend::InitMainRenderPass()
{
    VkAttachmentDescription colorAttachment = {
        .format = m_SwapchainFormat,
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription resolveAttachment = {
        .format = m_SwapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT, // Single sampled for presentation
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference resolveRef = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription depthAttachment = {
        .format = VK_FORMAT_D32_SFLOAT,
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depthRef = {
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
        .pResolveAttachments = &resolveRef,
        .pDepthStencilAttachment = &depthRef
    };

    VkAttachmentDescription attachments[3] = { colorAttachment, resolveAttachment, depthAttachment };

    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 3,
        .pAttachments = &attachments[0],
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VK_CHECK(vkCreateRenderPass(m_Device, &info, nullptr, &m_RenderPass));

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyRenderPass(m_Device, m_RenderPass, nullptr); 
    });
}

void RenderBackend::InitFramebuffers()
{
    VkFramebufferCreateInfo info = vkinit::FramebufferCreateInfo(m_RenderPass, m_Extent);
    VkFramebufferCreateInfo depthInfo = vkinit::FramebufferCreateInfo(m_DepthRenderPass, m_Extent);

    for (SwapchainImageData& image : m_Images) {
        VkImageView attachments[3] = { image.msaaColorView, image.view, image.depthView };
        info.attachmentCount = 3;
        info.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(m_Device, &info, nullptr, &image.framebuffer));

        depthInfo.attachmentCount = 1;
        depthInfo.pAttachments = &image.depthView;
        VK_CHECK(vkCreateFramebuffer(m_Device, &depthInfo, nullptr, &image.depthFramebuffer));

        m_DynamicDeletionQueue.PushFunction([=, this] { 
            vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr); 
            vkDestroyFramebuffer(m_Device, image.depthFramebuffer, nullptr); 
        });
    }
}

void RenderBackend::InitShadowResources()
{
    WritableTextureArrayCreateInfo info = {
        .resolution = SHADOWMAP_RESOLUTION,
        .size = MAX_SHADOWCASTERS,
        .format = VK_FORMAT_D32_SFLOAT,
        .useShadowSampler = true
    };

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        m_Frames[i].shadowArray.Init(m_Device, m_GraphicsQueue, m_ShadowRenderPass, m_Submitter, m_Allocator, info);
        m_MainDeletionQueue.PushFunction([=, this] {
            m_Frames[i].shadowArray.Cleanup(m_Device, m_Allocator);
        });
    };
}

void RenderBackend::InitResolveResources()
{
    VkExtent3D extent = { m_Extent.width, m_Extent.height, 1 };
    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, extent);
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };

    for (SwapchainImageData& image : m_Images) {
        vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &image.resolvedDepthImage.image, &image.resolvedDepthImage.allocation, nullptr);

        VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(VK_FORMAT_R32_SFLOAT, image.resolvedDepthImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &image.resolvedDepthView));

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vkDestroyImageView(m_Device, image.resolvedDepthView, nullptr);
            vmaDestroyImage(m_Allocator, image.resolvedDepthImage.image, image.resolvedDepthImage.allocation);
        });
    }
}

void RenderBackend::InitLightCullingDescriptor()
{
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .compareEnable = VK_FALSE,
        .unnormalizedCoordinates = VK_FALSE
    };
    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_DepthSampler));

    VkDescriptorSetLayoutBinding bindings[5];
    bindings[0] = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    };
    bindings[4] = {
        .binding = 4,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    };
    for (uint32_t i = 1; i < 4; i++) {
        bindings[i] = {
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        };
    };

    VkDescriptorSetLayoutCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 5,
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &createInfo, nullptr, &m_LightCullingDescriptorLayout));

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroySampler(m_Device, m_DepthSampler, nullptr);
        vkDestroyDescriptorSetLayout(m_Device, m_LightCullingDescriptorLayout, nullptr);
    });

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = sizeof(CameraUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU
    };
    for (FrameData& frame : m_Frames) {
        LightCullingData& lc = frame.lightCulling;

        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &lc.uniformBuffer.buffer, &lc.uniformBuffer.allocation, nullptr));
        vmaMapMemory(m_Allocator, lc.uniformBuffer.allocation, &lc.uniformBuffer.data);

        // Descriptor pool
        VkDescriptorPoolSize poolSizes[3] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
        };
        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .maxSets = 1,
            .poolSizeCount = 3,
            .pPoolSizes = poolSizes,
        };

        VkDescriptorPool pool;
        VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &pool));
        VkDescriptorSetAllocateInfo descriptorAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_LightCullingDescriptorLayout
        };
        vkAllocateDescriptorSets(m_Device, &descriptorAllocInfo, &lc.descriptorSet);

        VkDescriptorBufferInfo descriptorBufferInfo = {
            .buffer = lc.uniformBuffer.buffer,
            .offset = 0,
            .range = sizeof(CameraUniforms)
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = lc.descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &descriptorBufferInfo,
        };
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

        m_MainDeletionQueue.PushFunction([=, this] {
            vmaUnmapMemory(m_Allocator, lc.uniformBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, lc.uniformBuffer.buffer, lc.uniformBuffer.allocation);
            vkDestroyDescriptorPool(m_Device, pool, nullptr);
        });
    }
}

void RenderBackend::InitResolveDescriptor()
{
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };

    VkDescriptorSetLayoutCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &createInfo, nullptr, &m_ResolveDescriptorLayout));

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyDescriptorSetLayout(m_Device, m_ResolveDescriptorLayout, nullptr);
    });

    for (FrameData& frame : m_Frames) {
        // Descriptor pool
        VkDescriptorPoolSize poolSizes[2] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        };
        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .maxSets = 1,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes,
        };

        VkDescriptorPool pool;
        VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &pool));
        VkDescriptorSetAllocateInfo descriptorAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_ResolveDescriptorLayout
        };
        vkAllocateDescriptorSets(m_Device, &descriptorAllocInfo, &frame.resolveDescriptorSet);

        m_MainDeletionQueue.PushFunction([=, this] {
            vkDestroyDescriptorPool(m_Device, pool, nullptr);
        });
    }
}

void RenderBackend::InitLightCullingResources()
{
    m_TilesX = (m_Extent.width + TILE_SIZE - 1) / TILE_SIZE;
    m_TilesY = (m_Extent.height + TILE_SIZE - 1) / TILE_SIZE;

    uint32_t tileCount = m_TilesX * m_TilesY;
    uint32_t indexCount = tileCount * MAX_TILE_LIGHTS;
    INFO("Light culling tiles: " << m_TilesX << "x" << m_TilesY);

    for (FrameData& frame : m_Frames) {
        LightCullingData& lc = frame.lightCulling;

        // Light buffer
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .size = MAX_LIGHTS * sizeof(Light),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        };
        VmaAllocationCreateInfo allocInfo = {
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU
        };
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &lc.lightBuffer.buffer, &lc.lightBuffer.allocation, nullptr));
        vmaMapMemory(m_Allocator, lc.lightBuffer.allocation, &lc.lightBuffer.data);
        VkDescriptorBufferInfo lightBufferInfo = {
            .buffer = lc.lightBuffer.buffer,
            .offset = 0,
            .range = bufferInfo.size,
        };

        // Tile light indices
        bufferInfo.size = indexCount * sizeof(uint32_t);
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &lc.lightIndices.buffer, &lc.lightIndices.allocation, nullptr));
        VkDescriptorBufferInfo lightIndicesInfo = {
            .buffer = lc.lightIndices.buffer,
            .offset = 0,
            .range = bufferInfo.size,
        };

        // Tile light index counts
        bufferInfo.size = tileCount * sizeof(uint32_t);
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &lc.tileCounts.buffer, &lc.tileCounts.allocation, nullptr));
        VkDescriptorBufferInfo tileCountsInfo = {
            .buffer = lc.tileCounts.buffer,
            .offset = 0,
            .range = bufferInfo.size,
        };

        VkWriteDescriptorSet writes[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = lc.descriptorSet,
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &lightBufferInfo,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = lc.descriptorSet,
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &lightIndicesInfo,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = lc.descriptorSet,
                .dstBinding = 3,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &tileCountsInfo,
            },
        };
        vkUpdateDescriptorSets(m_Device, 3, writes, 0, nullptr);

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vmaUnmapMemory(m_Allocator, frame.lightCulling.lightBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, lc.lightBuffer.buffer, lc.lightBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, lc.lightIndices.buffer, lc.lightIndices.allocation);
            vmaDestroyBuffer(m_Allocator, lc.tileCounts.buffer, lc.tileCounts.allocation);
        });
    }
}

// NOTE: Shadow render pass and pipeline is same as depth but with MSAA disabled
void RenderBackend::InitShadowRenderPass()
{
    VkAttachmentDescription depthAttachment = {
        .format = VK_FORMAT_D32_SFLOAT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference depthRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 0,
        .pDepthStencilAttachment = &depthRef
    };

    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &depthAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VK_CHECK(vkCreateRenderPass(m_Device, &info, nullptr, &m_ShadowRenderPass));

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyRenderPass(m_Device, m_ShadowRenderPass, nullptr); 
    });
}

void RenderBackend::InitShadowPipeline()
{
    VkPushConstantRange constantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(DepthPushConstants),
    };
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&constantRange);
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_ShadowPipelineLayout)); 

    PipelineBuilder builder;

    VkShaderModule vert, frag;
    LoadShaderModule("src/Engine/Resource/Shader/depth.vert.spirv", vert);
    LoadShaderModule("src/Engine/Resource/Shader/depth.frag.spirv", frag);

    builder.MakeShader(vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert),
                       vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag));

    VertexInputDesc inputDesc = Vertex::GetDepthVertexDesc();
    VkPipelineRasterizationStateCreateInfo rasterizerInfo = vkinit::RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);
    rasterizerInfo.cullMode = VK_CULL_MODE_FRONT_BIT; // Helps with Peter-Panning
    builder.SetVertexInput(vkinit::VertexInputStateCreateInfo(&inputDesc.bindings, &inputDesc.attributes))
           .SetInputAssembly(vkinit::InputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
           .SetRasterizer(rasterizerInfo)
           .SetMultisampling(vkinit::MultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, false))
           .SetColorBlend(vkinit::ColorBlendAttachmentState())
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL))
           .SetPipelineLayout(m_ShadowPipelineLayout);

    m_ShadowPipeline = builder.BuildPipeline(m_Device, m_ShadowRenderPass, m_ShadowViewport, m_ShadowScissor);

    vkDestroyShaderModule(m_Device, vert, nullptr);
    vkDestroyShaderModule(m_Device, frag, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_ShadowPipelineLayout, nullptr); 
        vkDestroyPipeline(m_Device, m_ShadowPipeline, nullptr); 
    });
}

void RenderBackend::InitDepthRenderPass()
{
    VkAttachmentDescription depthAttachment = {
        .format = VK_FORMAT_D32_SFLOAT,
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depthRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 0,
        .pDepthStencilAttachment = &depthRef
    };

    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &depthAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VK_CHECK(vkCreateRenderPass(m_Device, &info, nullptr, &m_DepthRenderPass));

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyRenderPass(m_Device, m_DepthRenderPass, nullptr); 
    });
}

void RenderBackend::InitDepthPipeline()
{
    VkPushConstantRange constantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(DepthPushConstants),
    };
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&constantRange);
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_DepthPipelineLayout)); 

    PipelineBuilder builder;

    VkShaderModule vert, frag;
    LoadShaderModule("src/Engine/Resource/Shader/depth.vert.spirv", vert);
    LoadShaderModule("src/Engine/Resource/Shader/depth.frag.spirv", frag);

    builder.MakeShader(vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert),
                       vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag));

    VertexInputDesc inputDesc = Vertex::GetDepthVertexDesc();
    builder.SetVertexInput(vkinit::VertexInputStateCreateInfo(&inputDesc.bindings, &inputDesc.attributes))
           .SetInputAssembly(vkinit::InputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
           .SetRasterizer(vkinit::RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL))
           .SetMultisampling(vkinit::MultisampleStateCreateInfo(m_MSAASamples, false))
           .SetColorBlend(vkinit::ColorBlendAttachmentState())
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL))
           .SetPipelineLayout(m_DepthPipelineLayout);

    m_DepthPipeline = builder.BuildPipeline(m_Device, m_DepthRenderPass, m_Viewport, m_Scissor);

    vkDestroyShaderModule(m_Device, vert, nullptr);
    vkDestroyShaderModule(m_Device, frag, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_DepthPipelineLayout, nullptr); 
        vkDestroyPipeline(m_Device, m_DepthPipeline, nullptr); 
    });
}

void RenderBackend::InitLightCullingPipeline()
{
    // Pipeline
    VkPushConstantRange constantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(LightCullingPushConstants),
    };
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&constantRange);
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &m_LightCullingDescriptorLayout;
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_LightCullingPipelineLayout)); 

    PipelineBuilder builder;

    VkShaderModule comp;
    LoadShaderModule("src/Engine/Resource/Shader/lightCulling.comp.spirv", comp);

    VkPipelineShaderStageCreateInfo stage = vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp);

    VkComputePipelineCreateInfo computeInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stage,
        .layout = m_LightCullingPipelineLayout
    };
    VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &m_LightCullingPipeline));

    vkDestroyShaderModule(m_Device, comp, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_LightCullingPipelineLayout, nullptr); 
        vkDestroyPipeline(m_Device, m_LightCullingPipeline, nullptr); 
    });
}

void RenderBackend::InitResolvePipeline()
{
    // Pipeline
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(nullptr);
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &m_ResolveDescriptorLayout;
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_ResolvePipelineLayout)); 

    PipelineBuilder builder;

    VkShaderModule comp;
    LoadShaderModule("src/Engine/Resource/Shader/resolve.comp.spirv", comp);

    VkPipelineShaderStageCreateInfo stage = vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp);

    VkComputePipelineCreateInfo computeInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stage,
        .layout = m_ResolvePipelineLayout
    };
    VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &m_ResolvePipeline));

    vkDestroyShaderModule(m_Device, comp, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_ResolvePipelineLayout, nullptr); 
        vkDestroyPipeline(m_Device, m_ResolvePipeline, nullptr); 
    });
}

void RenderBackend::InitSyncStructures()
{
    VkFenceCreateInfo fenceInfo = vkinit::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreInfo = vkinit::SemaphoreCreateInfo();

    for (FrameData& frame : m_Frames) {
        VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.renderFence));
        VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &frame.acquireSemaphore));

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vkDestroyFence(m_Device, frame.renderFence, nullptr);
            vkDestroySemaphore(m_Device, frame.acquireSemaphore, nullptr);
        });
    }

    for (SwapchainImageData& image : m_Images) {
        image.flightFence = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &image.renderSemaphore));

        m_DynamicDeletionQueue.PushFunction([=, this] { 
            vkDestroySemaphore(m_Device, image.renderSemaphore, nullptr); 
        });
    }
}

void RenderBackend::InitMainPipelines()
{
    // Pipeline
    VkPushConstantRange constantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    VkDescriptorSetLayout layouts[3] = { m_DescriptorLayout, m_ArrayHandler.descriptorLayout, m_LightCullingDescriptorLayout };
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&constantRange);
    pipelineInfo.setLayoutCount = 3;
    pipelineInfo.pSetLayouts = layouts;
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_PipelineLayout)); 

    PipelineBuilder builder;

    VkShaderModule vert, frag;
    LoadShaderModule("src/Engine/Resource/Shader/basic.vert.spirv", vert);
    LoadShaderModule("src/Engine/Resource/Shader/basic.frag.spirv", frag);

    builder.MakeShader(vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert),
                       vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag));

    VertexInputDesc inputDesc = Vertex::GetVertexDesc();
    builder.SetVertexInput(vkinit::VertexInputStateCreateInfo(&inputDesc.bindings, &inputDesc.attributes))
           .SetInputAssembly(vkinit::InputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
           .SetRasterizer(vkinit::RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL))
           .SetMultisampling(vkinit::MultisampleStateCreateInfo(m_MSAASamples, false))
           .SetColorBlend(vkinit::ColorBlendAttachmentState())
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, false, VK_COMPARE_OP_LESS_OR_EQUAL))
           .SetPipelineLayout(m_PipelineLayout);

    m_Pipeline = builder.BuildPipeline(m_Device, m_RenderPass, m_Viewport, m_Scissor);

    builder.SetMultisampling(vkinit::MultisampleStateCreateInfo(m_MSAASamples, true))
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL));
    m_TransparencyPipeline = builder.BuildPipeline(m_Device, m_RenderPass, m_Viewport, m_Scissor);

    vkDestroyShaderModule(m_Device, vert, nullptr);
    vkDestroyShaderModule(m_Device, frag, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr); 
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr); 
        vkDestroyPipeline(m_Device, m_TransparencyPipeline, nullptr); 
    });
}

void RenderBackend::InitSSAOResources()
{
    // Generate SSAO kernel
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    std::vector<glm::vec4> ssaoKernel = std::vector<glm::vec4>(SSAO_SAMPLES);
    for (uint32_t i = 0; i < SSAO_SAMPLES; i++) {
        // Hemisphere around kernel centre
        glm::vec3 sample = {
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator)
        };
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);

        float scale = static_cast<float>(i) / static_cast<float>(SSAO_SAMPLES);

        // Lerp
        float a = 0.1;
        float b = 1.0;
        float f = scale * scale;
        scale = a + f * (b - a);

        // Bias samples closer to kernel centre
        sample *= scale;

        ssaoKernel[i] = glm::vec4(sample, 0.0); // padded for alignment
    }

    // Set SSAO kernel
    for (FrameData& frame : m_Frames) {
        memcpy(static_cast<char *>(frame.uniformBuffer.data) + offsetof(GlobalUniforms, ssaoKernel), ssaoKernel.data(), ssaoKernel.size() * sizeof(glm::vec4));
    }

    // Generate random SSAO noise image 
    std::vector<glm::vec3> ssaoNoise = std::vector<glm::vec3>(16);
    for (uint32_t i = 0; i < 16; i++) {
        glm::vec3 noise = {
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0,
            0.0
        };
        ssaoNoise[i] = noise;
    }

    VkExtent3D extent = { 4, 4, 1 };
    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(VK_FORMAT_R16G16B16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, extent);
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };
    vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_SSAONoiseImage.image, &m_SSAONoiseImage.allocation, nullptr);

    AllocatedBuffer stagingBuffer;
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = 4 * 4 * sizeof(glm::vec3),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo stagingBufferAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };
    VK_CHECK(vmaCreateBuffer(m_Allocator, &stagingBufferInfo, &stagingBufferAllocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

    vmaMapMemory(m_Allocator, stagingBuffer.allocation, &stagingBuffer.data);
    memcpy(stagingBuffer.data, ssaoNoise.data(), 4 * 4 * sizeof(glm::vec3));
    vmaUnmapMemory(m_Allocator, stagingBuffer.allocation);

    m_Submitter.ImmediateSubmit(m_Device, m_GraphicsQueue, [=, this](VkCommandBuffer commandBuffer) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = m_SSAONoiseImage.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region = {
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
            .imageExtent = extent,
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, m_SSAONoiseImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
    vmaDestroyBuffer(m_Allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(VK_FORMAT_R16G16B16_SFLOAT, m_SSAONoiseImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SSAONoiseView));

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_SSAONoiseSampler));

    VkDescriptorImageInfo descriptorImageInfo = {
        .sampler = m_SSAONoiseSampler,
        .imageView = m_SSAONoiseView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    for (FrameData& frame : m_Frames) {
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = frame.textureDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptorImageInfo,
        };
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyImageView(m_Device, m_SSAONoiseView, nullptr);
        vmaDestroyImage(m_Allocator, m_SSAONoiseImage.image, m_SSAONoiseImage.allocation);
        vkDestroySampler(m_Device, m_SSAONoiseSampler, nullptr);
    });
}

void RenderBackend::InitGlobalDescriptor()
{
    // Global descriptor set layout
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 1,
        .pBindings = &binding
    };
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorLayout));

    // Array descriptor set layout
    VkDescriptorSetLayoutBinding arrayBindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = TEXTURE_ARRAY_MAX_ENUM,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
    };
    layoutInfo.pBindings = arrayBindings;
    layoutInfo.bindingCount = 3;
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_ArrayHandler.descriptorLayout));
    
    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device, m_ArrayHandler.descriptorLayout, nullptr);
    });

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = sizeof(GlobalUniforms),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU
    };
    for (FrameData& frame : m_Frames) {
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr));
        vmaMapMemory(m_Allocator, frame.uniformBuffer.allocation, &frame.uniformBuffer.data);

        m_MainDeletionQueue.PushFunction([=, this] {
            vmaUnmapMemory(m_Allocator, frame.uniformBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);
        });

        VkDescriptorPoolSize poolSizes[2] = { 
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TEXTURE_ARRAY_MAX_ENUM + 2 } // + 1 for the shadow array, + 1 for ssao noise
        };
        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .maxSets = 2,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes,
        };
        VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &frame.descriptorPool));

        m_MainDeletionQueue.PushFunction([=, this] {
            vkDestroyDescriptorPool(m_Device, frame.descriptorPool, nullptr); 
        });

        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = frame.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_DescriptorLayout
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocInfo, &frame.descriptorSet));
        
        // Write uniform buffer descriptor set
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = frame.uniformBuffer.buffer,
            .offset = 0,
            .range = sizeof(GlobalUniforms)
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = frame.descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo
        };
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

        // Allocate texture array descriptor set
        allocInfo.pSetLayouts = &m_ArrayHandler.descriptorLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocInfo, &frame.textureDescriptorSet));
    }
}

void RenderBackend::LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module)
{
    // 'ate' flag puts the cursor at the end
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        ERROR("Failed to open shader file: " << path);
        abort();
    }

    // Get cursor pos (initialised at end)
    size_t fileSize = file.tellg();
    uint32_t *buf = new uint32_t[fileSize];

    file.seekg(0);
    file.read((char *) buf, fileSize);
    file.close();

    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .codeSize = fileSize,
        .pCode = buf,
    };

    VkShaderModule shaderModule;
    VkResult err = vkCreateShaderModule(m_Device, &info, nullptr, &shaderModule);
    if (err) {
        ERROR("Failed to create shader module: " << path);
        abort();
    }

    delete[] buf;

    module = shaderModule;
}
