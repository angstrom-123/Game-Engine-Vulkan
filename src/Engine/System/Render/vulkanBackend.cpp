#include "vulkanBackend.h"
#include "Component/shadowcaster.h"
#include "ECS/ecsTypes.h"
#include "System/Render/backendTypes.h"

#include <algorithm>
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

VulkanBackend::~VulkanBackend()
{
    VK_CHECK(vkDeviceWaitIdle(device));

    for (auto it = m_MainDeleter.rbegin(); it != m_MainDeleter.rend(); it++) {
        (*it)();
    }
    m_MainDeleter.clear();

    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) {
        (*it)();
    }
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
            .sampler = m_LinearSampler,
            .imageView = frontend.textureArrays[arrayId].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    };

    VkWriteDescriptorSet writes[FRAMES_IN_FLIGHT] = {};
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet3,
            .dstBinding = 0,
            .descriptorCount = TEXTURE_ARRAY_MAX_ENUM,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = arrayImageInfos
        };
    }
    vkUpdateDescriptorSets(device, FRAMES_IN_FLIGHT, writes, 0, nullptr);

    // ================================================== Shadow Array ==================================================

    frontend.shadowsEnabled = info.maxShadows > 0;
    frontend.shadowArray.Init(std::max(info.maxShadows, 1u), *this);
    VkDescriptorImageInfo shadowImageInfo = {
        .sampler = m_ComparisonSampler,
        .imageView = frontend.shadowArray.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    // Lighting descriptor set
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = frames[i].descriptorSet2,
            .dstBinding = 1,
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
        if (array.initialized) {
            array.Cleanup(device, allocator);
        }
    }

    if (frontend.shadowArray.initialized) {
        frontend.shadowArray.Cleanup(device, allocator);
    }
}

void VulkanBackend::Draw(ECS *ecs, GraphicsFrontend& frontend, const std::set<Entity>& entities)
{
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

    // Update uniform buffer
    PerFrameUniforms *uniforms = static_cast<PerFrameUniforms *>(frame.uniformBuffer.data);
    uniforms->camera = {
        .view = camera.view,
        .proj = camera.projection,
        .invView = glm::inverse(camera.view),
        .invProj = glm::inverse(camera.projection),
        .position = cameraPos
    };
    uniforms->settings = {
        .exposure = 1.0,
        .gamma = 2.2,
        .ambientIntensity = 0.15,
        .lightCount = lightCount,
        .screenSize = glm::ivec2(m_Extent.width, m_Extent.height),
        .shadowSize = glm::ivec2(SHADOW_RESOLUTION),
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
        VkDescriptorSet descriptorSets[4] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet, 
            frame.dummyDescriptorSet, 
            frame.descriptorSet3 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthPipelineLayout, 
                0, 4, descriptorSets, 0, nullptr);
        for (const Entity e : visibleOpaqueEntities) {
            VertexPushConstants vertexConstants = {
                .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_DepthPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                    0, sizeof(VertexPushConstants), 
                    &vertexConstants);

            FragmentPushConstants fragmentConstants = { 
                .material = ecs->GetComponent<Material>(e).Pack(),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_DepthPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
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
        
    } // ================================================== (END) Depth Pre-Pass ==================================================

    { // ================================================== GBuffer Pass ==================================================

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
        VkDescriptorSet descriptorSets[4] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet, 
            frame.dummyDescriptorSet, 
            frame.descriptorSet3 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufferPipelineLayout, 
                0, 4, descriptorSets, 0, nullptr);
        for (const Entity e : visibleOpaqueEntities) {
            VertexPushConstants vertexConstants = {
                .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_GBufferPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                    0, sizeof(VertexPushConstants), 
                    &vertexConstants);

            FragmentPushConstants fragmentConstants = { 
                .material = ecs->GetComponent<Material>(e).Pack(),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_GBufferPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
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

    } // ================================================== (END) GBuffer Pass ==================================================

    { // ================================================== Shadowcaster Pass ==================================================

        // TODO: Could add frustum culling here per light
        if (frontend.shadowsEnabled) {
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

            const auto [shadowcasterCount, shadowcasters] = ecs->GetData<Shadowcaster>();
            for (uint32_t i = 0; i < shadowcasterCount; i++) {
                const Shadowcaster& shadowcaster = shadowcasters[i];
                ASSERT(shadowcaster.shadowIndex != UINT32_MAX && "Unallocated shadowcaster");

                VkRenderingAttachmentInfoKHR depthAttachment = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                    .imageView = frontend.shadowArray.arrayViews[shadowcaster.shadowIndex],
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
                glm::mat4x4 shadowcasterVP = shadowcaster.projection * shadowcaster.view;
                for (const Entity e : opaqueEntities) {
                    ShadowPushConstants shadowConstants = {
                        .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
                        .vp = shadowcasterVP,
                    };
                    vkCmdPushConstants(frame.commandBuffer, 
                            m_ShadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                            0, sizeof(ShadowPushConstants), 
                            &shadowConstants);

                    const Mesh& mesh = ecs->GetComponent<Mesh>(e);
                    VkDeviceSize offsets = 0;
                    vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
                    vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
                }
                m_PFNCmdEndRendering(frame.commandBuffer);
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

    } // ================================================== (END) Shadowcaster Pass ==================================================

    { // ================================================== Lighting Compute Pass ==================================================

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
            frame.descriptorSet2 };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipelineLayout, 
                0, 3, descriptorSets, 0, nullptr);
        const uint32_t GROUP_SIZE = 16;
        uint32_t groupsX = (m_Extent.width + GROUP_SIZE - 1) / GROUP_SIZE;
        uint32_t groupsY = (m_Extent.height + GROUP_SIZE - 1) / GROUP_SIZE;
        vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

    } // ================================================== (END) Lighting Compute Pass ==================================================

    { // ================================================== Transparency Pass ==================================================

        // Transition HDR image to attachment
        VkImageMemoryBarrier hdrBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
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
        VkDescriptorSet descriptorSets[4] = { 
            frame.descriptorSet0, 
            frame.dummyDescriptorSet, 
            frame.descriptorSet2, 
            frame.descriptorSet3 
        };
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_TransparencyPipelineLayout, 
                0, 4, descriptorSets, 0, nullptr);
        for (const auto& [_, e] : visibleTransparentEntities) {
            VertexPushConstants vertexConstants = {
                .model = ecs->GetComponent<Transform>(e).GlobalModelMatrix(ecs),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_TransparencyPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                    0, sizeof(VertexPushConstants), 
                    &vertexConstants);

            FragmentPushConstants fragmentConstants = { 
                .material = ecs->GetComponent<Material>(e).Pack(),
            };
            vkCmdPushConstants(frame.commandBuffer, 
                    m_TransparencyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                    sizeof(VertexPushConstants), sizeof(FragmentPushConstants), 
                    &fragmentConstants);

            const Mesh& mesh = ecs->GetComponent<Mesh>(e);
            VkDeviceSize offsets = 0;
            vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offsets);
            vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
        }
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition hdr image to shader readable
        hdrBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hdrBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        hdrBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);

    } // ================================================== (END) Transparency ==================================================

    { // ================================================== Tone-Mapping Pass ==================================================

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
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_ToneMapPipelineLayout, 
                0, 1, &frame.descriptorSet0, 0, nullptr);
        vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
        m_PFNCmdEndRendering(frame.commandBuffer);

        // Transition tone mapped attachment to shader readable 
        toneMapBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toneMapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toneMapBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toneMapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(frame.commandBuffer, 
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &toneMapBarrier);

    } // ================================================== (END) Tone-Mapping Pass ==================================================

    { // ================================================== (SWAPCHAIN) Anti-Aliasing Pass ==================================================

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
        AntiAliasingPushConstants antiAliasingConstants = {
            .padding = glm::vec4(0.0)
        };
        vkCmdPushConstants(frame.commandBuffer, 
                m_AntiAliasingPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                0, sizeof(AntiAliasingPushConstants), 
                &antiAliasingConstants);
        vkCmdBindDescriptorSets(frame.commandBuffer, 
                VK_PIPELINE_BIND_POINT_GRAPHICS, m_AntiAliasingPipelineLayout, 
                0, 1, &frame.descriptorSet0, 0, nullptr);
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

    } // ================================================== (END) (SWAPCHAIN) Anti-Aliasing Pass ==================================================

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

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

    return (AllocatedTexture) {
        .arrayID = arrayID,
        .layerID = layerIndex,
    };
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
    for (auto it = m_DynamicDeleter.rbegin(); it != m_DynamicDeleter.rend(); it++) {
        (*it)();
    }
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

uint32_t VulkanBackend::AllocateShadowcaster(GraphicsFrontend& frontend)
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

    // ================================================== Albedo Target ==================================================

    imageCreateInfo.format = ALBEDO_FORMAT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_AlbedoTarget.image.image, &m_AlbedoTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_AlbedoTarget.image.image;
    viewCreateInfo.format = ALBEDO_FORMAT;
    viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_AlbedoTarget.view));

    // ================================================== Normal Target ==================================================

    imageCreateInfo.format = NORMAL_FORMAT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_NormalTarget.image.image, &m_NormalTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_NormalTarget.image.image;
    viewCreateInfo.format = NORMAL_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_NormalTarget.view));

    // ================================================== Material Target ==================================================

    imageCreateInfo.format = MATERIAL_FORMAT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_MaterialTarget.image.image, &m_MaterialTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_MaterialTarget.image.image;
    viewCreateInfo.format = MATERIAL_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_MaterialTarget.view));

    // ================================================== Lighting Target ==================================================

    imageCreateInfo.format = LIGHTING_FORMAT;
    imageCreateInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_LightingTarget.image.image, &m_LightingTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_LightingTarget.image.image;
    viewCreateInfo.format = LIGHTING_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_LightingTarget.view));

    // ================================================== Tone-Map Target ==================================================

    imageCreateInfo.format = TONE_MAP_FORMAT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &m_ToneMapTarget.image.image, &m_ToneMapTarget.image.allocation, nullptr);

    viewCreateInfo.image = m_ToneMapTarget.image.image;
    viewCreateInfo.format = TONE_MAP_FORMAT;
    VK_CHECK(vkCreateImageView(device, &viewCreateInfo, nullptr, &m_ToneMapTarget.view));

    // ================================================== Cleanup ==================================================

    m_DynamicDeleter.push_back([=, this] {
        vkDestroyImageView(device, m_DepthTarget.view, nullptr);
        vkDestroyImageView(device, m_AlbedoTarget.view, nullptr);
        vkDestroyImageView(device, m_NormalTarget.view, nullptr);
        vkDestroyImageView(device, m_MaterialTarget.view, nullptr);
        vkDestroyImageView(device, m_LightingTarget.view, nullptr);
        vkDestroyImageView(device, m_ToneMapTarget.view, nullptr);
        vmaDestroyImage(allocator, m_DepthTarget.image.image, m_DepthTarget.image.allocation);
        vmaDestroyImage(allocator, m_AlbedoTarget.image.image, m_AlbedoTarget.image.allocation);
        vmaDestroyImage(allocator, m_NormalTarget.image.image, m_NormalTarget.image.allocation);
        vmaDestroyImage(allocator, m_MaterialTarget.image.image, m_MaterialTarget.image.allocation);
        vmaDestroyImage(allocator, m_LightingTarget.image.image, m_LightingTarget.image.allocation);
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
    // ================================================== Frame Buffers ==================================================

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
    VmaAllocationCreateInfo bufferAllocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU
    };
    for (FrameData& frame : frames) {
        vmaCreateBuffer(allocator, &uniformBufferInfo, &bufferAllocInfo, &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.uniformBuffer.allocation, &frame.uniformBuffer.data);

        vmaCreateBuffer(allocator, &lightBufferInfo, &bufferAllocInfo, &frame.lightBuffer.buffer, &frame.lightBuffer.allocation, nullptr);
        vmaMapMemory(allocator, frame.lightBuffer.allocation, &frame.lightBuffer.data);

        m_MainDeleter.push_back([=, this] {
            vmaUnmapMemory(allocator, frame.uniformBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);
            vmaUnmapMemory(allocator, frame.lightBuffer.allocation);
            vmaDestroyBuffer(allocator, frame.lightBuffer.buffer, frame.lightBuffer.allocation);
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
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_LinearSampler));

    // samplerInfo.maxLod = 4.0;
    // samplerInfo.mipLodBias = -0.5;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_NormalSampler));

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_GREATER;
    samplerInfo.maxLod = 1.0;
    samplerInfo.mipLodBias = 0.0;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 0.0;
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &m_ComparisonSampler));

    m_MainDeleter.push_back([=, this] {
        vkDestroySampler(device, m_LinearSampler, nullptr);
        vkDestroySampler(device, m_NormalSampler, nullptr);
        vkDestroySampler(device, m_ComparisonSampler, nullptr);
    });
}

void VulkanBackend::InitDescriptors()
{
    const uint32_t FIF = FRAMES_IN_FLIGHT;
    VkDescriptorPoolSize poolSizes[8] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FIF },                                  // Uniform buffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * 4 },                      // 4x GBuffer
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF * TEXTURE_ARRAY_MAX_ENUM }, // MAXx Texture arrays
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF },                          // HDR sampler
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF },                          // LDR sampler
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FIF },                          // Shadow array
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FIF },                                   // HDR storage image
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FIF },                                  // Light buffer
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES_IN_FLIGHT * 5,
        .poolSizeCount = 8,
        .pPoolSizes = poolSizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool));

    // ================================================== Create Dummy Set ==================================================

    VkDescriptorSetLayoutCreateInfo layoutInfoDummy = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfoDummy, nullptr, &m_DummyDescriptorLayout));

    // ================================================== Create Set 0 (Globals) ==================================================

    VkDescriptorSetLayoutBinding bindings0[3] = {
        { // Uniform buffer
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
        },
        { // HDR sampler
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
        },
        { // LDR sampler
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo0 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings0
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo0, nullptr, &m_DescriptorLayout0));

    // ================================================== Create Set 1 (Deferred Shading) ==================================================

    VkDescriptorSetLayoutBinding bindings1[2] = {
        { // GBuffer array
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 4,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        { // HDR image
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo1 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings1
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo1, nullptr, &m_DescriptorLayout1));

    // ================================================== Create Set 2 (All Lighting) ==================================================

    VkDescriptorSetLayoutBinding bindings2[2] = {
        { // Light buffer
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        },
        { // Shadow array
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo2 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings2
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo2, nullptr, &m_DescriptorLayout2));

    // ================================================== Create Set 3 (Textures) ==================================================

    VkDescriptorSetLayoutBinding bindings3 = {
        // Texture arrays
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = TEXTURE_ARRAY_MAX_ENUM,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo3 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &bindings3
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo3, nullptr, &m_DescriptorLayout3));

    m_MainDeleter.push_back([=, this] {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, m_DummyDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, m_DescriptorLayout0, nullptr);
        vkDestroyDescriptorSetLayout(device, m_DescriptorLayout1, nullptr);
        vkDestroyDescriptorSetLayout(device, m_DescriptorLayout2, nullptr);
        vkDestroyDescriptorSetLayout(device, m_DescriptorLayout3, nullptr);
    });

    for (FrameData& frame : frames) {

        // ================================================== Allocate ==================================================

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

        // ================================================== Write Set 0 ==================================================

        {
            VkDescriptorBufferInfo uniformBufferInfo = {
                .buffer = frame.uniformBuffer.buffer,
                .range = sizeof(PerFrameUniforms)
            };

            VkDescriptorImageInfo hdrSamplerInfo = {
                .sampler = m_LinearSampler,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo ldrSamplerInfo = {
                .sampler = m_LinearSampler,
                .imageView = m_ToneMapTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
                { // HDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &hdrSamplerInfo
                },
                { // LDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &ldrSamplerInfo
                }
            };

            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        }

        // ================================================== Write Set 1 ==================================================

        {
            VkDescriptorImageInfo gBufferArrayInfo[4] = {
                { m_LinearSampler, m_AlbedoTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_NormalSampler, m_NormalTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_LinearSampler, m_MaterialTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_LinearSampler, m_DepthTarget.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
            };

            VkDescriptorImageInfo hdrStorageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };

            VkWriteDescriptorSet writes[2] = {
                { // GBuffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 0,
                    .descriptorCount = 4,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = gBufferArrayInfo
                },
                { // HDR storage image
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &hdrStorageInfo
                },
            };

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        // ================================================== Write Set 2 ==================================================

        {
            VkDescriptorBufferInfo lightBufferInfo = {
                .buffer = frame.lightBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkWriteDescriptorSet writes[1] = {
                { // Light buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &lightBufferInfo
                }
                // Binding 1 = shadow array = not written here.
            };

            vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
        }

        // ================================================== Write Set 3 ==================================================

        // Just the texture array - not written here
    }
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

            VkDescriptorImageInfo hdrSamplerInfo = {
                .sampler = m_LinearSampler,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            VkDescriptorImageInfo ldrSamplerInfo = {
                .sampler = m_LinearSampler,
                .imageView = m_ToneMapTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
                { // HDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &hdrSamplerInfo
                },
                { // LDR sampler
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet0,
                    .dstBinding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &ldrSamplerInfo
                }
            };

            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        }

        // ================================================== Write Set 1 ==================================================

        {
            VkDescriptorImageInfo gBufferArrayInfo[4] = {
                { m_LinearSampler, m_AlbedoTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_NormalSampler, m_NormalTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_LinearSampler, m_MaterialTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { m_LinearSampler, m_DepthTarget.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
            };

            VkDescriptorImageInfo hdrStorageInfo = {
                .sampler = VK_NULL_HANDLE,
                .imageView = m_LightingTarget.view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            };

            VkWriteDescriptorSet writes[2] = {
                { // GBuffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 0,
                    .descriptorCount = 4,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = gBufferArrayInfo
                },
                { // HDR storage image
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet1,
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &hdrStorageInfo
                }
            };

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        // ================================================== Write Set 2 ==================================================

        {
            VkDescriptorBufferInfo lightBufferInfo = {
                .buffer = frame.lightBuffer.buffer,
                .range = VK_WHOLE_SIZE
            };

            VkWriteDescriptorSet writes[1] = {
                { // Light buffer
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = frame.descriptorSet2,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &lightBufferInfo
                },
                // Binding 1 = shadow arrays - not written here
            };

            vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);
        }

        // ================================================== Write Set 3 ==================================================

        // Just the texture array - not written here
    }
}

void VulkanBackend::InitPipelines()
{
    // ================================================== Push Constants ==================================================

    VkPushConstantRange mainConstantRanges[2] = {
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

    // ================================================== Depth Layout ==================================================

    {
        VkDescriptorSetLayout depthLayouts[4] = { 
            m_DescriptorLayout0, 
            m_DummyDescriptorLayout, 
            m_DummyDescriptorLayout, 
            m_DescriptorLayout3 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 4,
            .pSetLayouts = depthLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_DepthPipelineLayout));
    }

    // ================================================== Tone-Mapping Layout ==================================================

    {
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_DescriptorLayout0
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_ToneMapPipelineLayout));
    }

    // ================================================== Anti-Aliasing Layout ==================================================

    {
        VkPushConstantRange antiAliasingConstantRange = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .size = sizeof(AntiAliasingPushConstants),
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_DescriptorLayout0,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &antiAliasingConstantRange
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_AntiAliasingPipelineLayout));
    }

    // ================================================== GBuffer Layout ==================================================

    {
        // Need descriptor layout 3, so need to pad with dummy descriptors to get the index right
        VkDescriptorSetLayout gBufferLayouts[4] = { 
            m_DescriptorLayout0, 
            m_DummyDescriptorLayout, 
            m_DummyDescriptorLayout, 
            m_DescriptorLayout3 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 4,
            .pSetLayouts = gBufferLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_GBufferPipelineLayout));
    }

    // ================================================== Lighting Layouts ==================================================

    {
        VkDescriptorSetLayout lightingLayouts[3] = { m_DescriptorLayout0, m_DescriptorLayout1, m_DescriptorLayout2 };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 3,
            .pSetLayouts = lightingLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_LightingPipelineLayout));
    }

    // ================================================== Transparency Layout ==================================================

    {
        VkDescriptorSetLayout transparencyLayouts[4] = { 
            m_DescriptorLayout0, 
            m_DummyDescriptorLayout, 
            m_DescriptorLayout2, 
            m_DescriptorLayout3 
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 4,
            .pSetLayouts = transparencyLayouts,
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = mainConstantRanges
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_TransparencyPipelineLayout));
    }

    // ================================================== Shadow Layout ==================================================

    {
        VkPushConstantRange shadowConstantRange = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(ShadowPushConstants),
        };
        VkPipelineLayoutCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_DescriptorLayout0,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &shadowConstantRange,
        };
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &m_ShadowPipelineLayout));
    }

    m_MainDeleter.push_back([=, this] {
        vkDestroyPipelineLayout(device, m_DepthPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_ShadowPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_GBufferPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_LightingPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_TransparencyPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_ToneMapPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, m_AntiAliasingPipelineLayout, nullptr);
    });

    // ================================================== Graphics Pipelines ==================================================

    VertexInputDesc depthVertexDesc = Vertex::GetDepthVertexDesc();
    m_DepthPipeline = CreateGraphicsPipeline({
        .pipelineName = "Depth",
        .vertexShader = "depth.vert",
        .fragmentShader = "depth.frag",
        .viewport = &m_Viewport,
        .scissor = &m_ScissorRect,
        .pipelineLayout = m_DepthPipelineLayout,
        .vertexInput = &depthVertexDesc,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthFlags = PIPELINE_DEPTH_TEST | PIPELINE_DEPTH_WRITE,
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
        .depthFlags = PIPELINE_DEPTH_TEST | PIPELINE_DEPTH_WRITE | PIPELINE_DEPTH_BIAS,
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
        .pipelineLayout = m_GBufferPipelineLayout,
        .vertexInput = &vertexDesc,
        .depthFlags = PIPELINE_DEPTH_TEST,
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
        .pipelineLayout = m_TransparencyPipelineLayout,
        .vertexInput = &vertexDesc,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .blendEnable = true,
        .depthFlags = PIPELINE_DEPTH_TEST,
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
        .pipelineLayout = m_ToneMapPipelineLayout, 
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
        .pipelineLayout = m_AntiAliasingPipelineLayout, 
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .depthCompare = VK_COMPARE_OP_ALWAYS, 
        .attachmentFormats = { m_SwapchainFormat}
    });

    // ================================================== Compute Pipelines ==================================================

    VkShaderModule lightingComputeModule = LoadShaderModule("src/Engine/Resource/Shader/lighting.comp.spirv");
    VkComputePipelineCreateInfo lightingComputePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = lightingComputeModule,
            .pName = "main"
        },
        .layout = m_LightingPipelineLayout
    };
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &lightingComputePipelineInfo, nullptr, &m_LightingPipeline));
    vkDestroyShaderModule(device, lightingComputeModule, nullptr);
    m_MainDeleter.push_back([=, this] {
        vkDestroyPipeline(device, m_LightingPipeline, nullptr);
    });
}

VkPipeline VulkanBackend::CreateGraphicsPipeline(const GraphicsPipelineCreateInfo&& info)
{
    INFO("Creating graphics pipeline: " << info.pipelineName);

    bool depthTest = FLAGS_CONTAIN(info.depthFlags, PIPELINE_DEPTH_TEST);
    bool depthWrite = FLAGS_CONTAIN(info.depthFlags, PIPELINE_DEPTH_WRITE);
    bool depthBias = FLAGS_CONTAIN(info.depthFlags, PIPELINE_DEPTH_BIAS);

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

#ifdef PROFILING
#include "Util/profiler.h"

#error "Profiling v2 not implemented yet"

void VulkanBackend::InitProfiling()
{
    std::string headings[7] = { "Depth", "Resolve", "Culling", "Shadow", "Opaque", "Translucent", "Total" };
    PROFILER_BEGIN_GPU_SESSION(headings, 7);

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
        frame.queryCount = 12;

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
