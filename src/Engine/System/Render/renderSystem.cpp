#include "renderSystem.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "Component/light.h"
#include "Component/camera.h"
#include "ECS/ecsTypes.h"
#include "Geometry/frustum.h"
#include "System/Render/initialiser.h"
#include "System/Render/pipeline.h"
#include "System/Render/renderTypes.h"
#include "Util/allocator.h"
#include "Util/logger.h"
#include "Util/imageLoader.h"
#include "Component/transform.h"
#include "Component/material.h"

#include <VkBootstrap/VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <ranges>
#include <vulkan/vulkan_core.h>
#include <stb/stb_image.h>

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

void RenderSystem::Init(GLFWwindow *window, Config *config)
{
    m_Extent.width = config->windowWidth;
    m_Extent.height = config->windowHeight;
    m_PresentMode = config->vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    m_DrawOrder.reserve(MAX_ENTITIES);
    m_MaxTextureArrays = 2;

    InitVulkan(window);
    InitMSAA(config->msaaSamples);
    InitSwapchain();
    InitGlobalDescriptor();
    InitCommands();
    InitDefaultRenderPass();
    InitDepthRenderPass();
    InitFramebuffers();
    InitSyncStructures();
    InitLightCulling();
    InitResolve();
    InitLightCullingResources();
    InitResolveResources();
    InitDepthPipeline();
    InitLightCullingPipeline();
    InitResolvePipeline();
    InitMainPipelines();

    ImageData whiteImage, normalImage;
    whiteImage.LoadImage("src/Engine/Resource/Texture/white.png", false);
    normalImage.LoadImage("src/Engine/Resource/Texture/normal.png", false);
    m_ColorArrayIndex = CreateTextureArray(2048, 64, VK_FORMAT_R8G8B8A8_SRGB);
    m_DataArrayIndex = CreateTextureArray(2048, 32, VK_FORMAT_R8G8B8A8_UNORM);
    m_DefaultColorTextureIndex = AllocateTexture(&whiteImage, m_TextureArrays[m_ColorArrayIndex]);
    m_DefaultNormalTextureIndex = AllocateTexture(&normalImage, m_TextureArrays[m_DataArrayIndex]);

    m_IsInitialized = true;
}

void RenderSystem::SetCamera(Entity camera)
{
    m_Camera = camera;
}

void RenderSystem::Cleanup() 
{
    if (m_IsInitialized) {
        VK_CHECK(vkDeviceWaitIdle(m_Device));

        m_MainDeletionQueue.Flush();
        m_DynamicDeletionQueue.Flush();

        vmaDestroyAllocator(m_Allocator);
        vkDestroyDevice(m_Device, nullptr);
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
        vkDestroyInstance(m_Instance, nullptr);
    }
}

void RenderSystem::Update(ECS& ecs)
{
    if (m_DidResize) {
        Resize(ecs);
        m_DidResize = false;
    }

    Camera& cam = ecs.GetComponent<Camera>(m_Camera);
    Transform& camTransform = ecs.GetComponent<Transform>(m_Camera);
    glm::vec3 camTranslation = camTransform.translation;
    if (camTransform.inherit != INVALID_HANDLE) {
        camTranslation += ecs.GetComponent<Transform>(camTransform.inherit).translation;
    }
    glm::mat4x4 camVP = cam.projection * cam.view;
    Frustum camFrustum = Frustum(camVP);

    // Split up opaque and translucent entities, and perform frustum culling 
    // TODO: Cache the draw order
    int32_t entityCount = entities.size();
    int32_t frontPtr = 0;
    int32_t backPtr = entityCount - 1;
    for (const Entity e : entities) {
        // Frustum culling
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        glm::mat4x4 model = transform.ModelMatrix();
        if (transform.inherit != INVALID_HANDLE) {
            model = ecs.GetComponent<Transform>(transform.inherit).ModelMatrix() * model;
        }
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

    // ============== Acquire swapchain image and wait on fences ===============

    size_t frameIndex = m_FrameNum % FRAMES_IN_FLIGHT;
    FrameData &frame = m_Frames[frameIndex];

    VK_CHECK(vkWaitForFences(m_Device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_Device, 1, &frame.renderFence));

    uint32_t imageIndex;
    VkResult acquireErr = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, frame.acquireSemaphore, nullptr, &imageIndex);
    if (acquireErr == VK_ERROR_OUT_OF_DATE_KHR || acquireErr == VK_SUBOPTIMAL_KHR) {
        m_DidResize = true;
        return;
    } else if (acquireErr == VK_NOT_READY) {
        return;
    } 
    VK_CHECK(acquireErr);

    SwapchainImageData &image = m_Images[imageIndex];

    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    if (image.flightFence != VK_NULL_HANDLE && image.flightFence != frame.renderFence) {
        VK_CHECK(vkWaitForFences(m_Device, 1, &image.flightFence, VK_TRUE, UINT64_MAX));
    }
    image.flightFence = frame.renderFence;

    // ======================= Begin recording commands ========================

    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_Scissor);

    VkClearValue colorClear = { .color = {{ 0.5, 0.7, 1.0, 1.0 }} };
    VkClearValue resolveClear = { .color = {{ 0.0, 0.0, 0.0, 0.0 }} };
    VkClearValue depthClear = { .depthStencil = { .depth = 1.0f } };
    VkClearValue clearValues[3] = { colorClear, resolveClear, depthClear };

    // ============================ Depth pre-pass =============================

    VkRenderPassBeginInfo depthPassInfo = vkinit::RenderPassBeginInfo(m_DepthRenderPass, image.depthFramebuffer, &depthClear, 1, m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &depthPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthPipeline);

    // Only draw opaque entities to depth buffer
    for (int32_t i = 0; i < frontPtr; i++) {
        const Entity e = m_DrawOrder[i];

        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        glm::mat4x4 model = transform.ModelMatrix();
        if (transform.inherit != INVALID_HANDLE) {
            model = ecs.GetComponent<Transform>(transform.inherit).ModelMatrix() * model;
        }

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

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipeline);

    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipelineLayout, 0, 1, &frame.resolveDescriptorSet, 0, nullptr);

    const uint32_t GROUP_SIZE = 8;
    uint32_t groupsX = (m_Extent.width + GROUP_SIZE - 1) / GROUP_SIZE;
    uint32_t groupsY = (m_Extent.height + GROUP_SIZE - 1) / GROUP_SIZE;
    vkCmdDispatch(frame.commandBuffer, groupsX, groupsY, 1);

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

    VkMemoryBarrier memoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

    // Transition resolved image back to general for submit
    barrierToGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToGeneral);

    // ========================= Opaque forward+ pass ==========================

    VkRenderPassBeginInfo passInfo = vkinit::RenderPassBeginInfo(m_RenderPass, image.framebuffer, &clearValues[0], 3, m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    GlobalUniforms uniforms = {
        .vp = cam.projection * cam.view,
    };
    memcpy(frame.uniformBuffer.data, &uniforms, sizeof(GlobalUniforms));

    VkDescriptorSet sets[3] = { frame.descriptorSet, frame.arrayDescriptorSet, frame.lightCulling.descriptorSet };
    vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 3, sets, 0, nullptr);

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    for (int32_t i = 0; i < frontPtr; i++) {
        const Entity e = m_DrawOrder[i];
        
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);

        // Apply local transform and any inherited transform
        glm::mat4x4 model = transform.ModelMatrix();
        if (transform.inherit != INVALID_HANDLE) {
            model = ecs.GetComponent<Transform>(transform.inherit).ModelMatrix() * model;
        }

        ASSERT(mesh.allocated && "Drawing unallocated mesh");

        Material& material = ecs.GetComponent<Material>(e);

        PushConstants constants = {
            .model = model,
            .specularExponent = material.specularExponent,
            .ambientIndex = material.ambientTextureIndex,
            .diffuseIndex = material.diffuseTextureIndex,
            .normalIndex = material.normalTextureIndex,
            .tileSize = TILE_SIZE,
            .tilesX = m_TilesX,
            .tilesY = m_TilesY,
            .maxTileLights = MAX_TILE_LIGHTS,
        };
        vkCmdPushConstants(frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
    }

    // ======================= Translucent forward+ pass =======================

    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TransparencyPipeline);
    for (int32_t i = frontPtr; i < entityCount; i++) {
        const Entity e = m_DrawOrder[i];
        
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);

        // Apply local transform and any inherited transform
        glm::mat4x4 model = transform.ModelMatrix();
        if (transform.inherit != INVALID_HANDLE) {
            model = ecs.GetComponent<Transform>(transform.inherit).ModelMatrix() * model;
        }

        ASSERT(mesh.allocated && "Drawing unallocated mesh");

        Material& material = ecs.GetComponent<Material>(e);

        PushConstants constants = {
            .model = model,
            .specularExponent = material.specularExponent,
            .ambientIndex = material.ambientTextureIndex,
            .diffuseIndex = material.diffuseTextureIndex,
            .normalIndex = material.normalTextureIndex,
            .tileSize = TILE_SIZE,
            .tilesX = m_TilesX,
            .tilesY = m_TilesY,
            .maxTileLights = MAX_TILE_LIGHTS,
        };
        vkCmdPushConstants(frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &constants);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &mesh.vertexBuffer.buffer, &offset);
        vkCmdDraw(frame.commandBuffer, mesh.vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(frame.commandBuffer);

    VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = vkinit::SubmitInfo(&frame, &image, &waitStage);
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, frame.renderFence));

    VkPresentInfoKHR presentInfo = vkinit::PresentInfo(&image, &m_Swapchain, &imageIndex);
    VkResult presentErr = vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
    if (presentErr == VK_ERROR_OUT_OF_DATE_KHR || presentErr == VK_SUBOPTIMAL_KHR) {
        m_DidResize = true;
    } else {
        VK_CHECK(presentErr);
    }

    m_FrameNum++;
}

void RenderSystem::RequestResize()
{
    m_DidResize = true;
}

void RenderSystem::AllocateMesh(Mesh& mesh)
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
    ImmediateSubmit([=](VkCommandBuffer commandBuffer) {
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

void RenderSystem::AllocateMaterialTextures(const MaterialTextureInfo& info, Material& mat)
{
    mat.ambientTextureIndex = (info.ambientTextureData)
            ? AllocateTexture(info.ambientTextureData, m_TextureArrays[m_ColorArrayIndex])
            : m_DefaultColorTextureIndex;

    mat.diffuseTextureIndex = (info.diffuseTextureData)
            ? AllocateTexture(info.diffuseTextureData, m_TextureArrays[m_ColorArrayIndex])
            : m_DefaultColorTextureIndex;

    // NOTE: Normal texture is allocated to a special "data array" with a different image format
    mat.normalTextureIndex = (info.normalTextureData)
            ? AllocateTexture(info.normalTextureData, m_TextureArrays[m_DataArrayIndex])
            : m_DefaultNormalTextureIndex;
}

void RenderSystem::Resize(ECS& ecs)
{
    auto getCapabilities = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(getCapabilities(m_PhysicalDevice, m_Surface, &capabilities));

    if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0 || capabilities.currentExtent.width == UINT32_MAX || capabilities.currentExtent.height == UINT32_MAX) {
        return;
    }

    if (capabilities.currentExtent.width == m_Extent.width && capabilities.currentExtent.height == m_Extent.height) {
        return;
    }

    VK_CHECK(vkDeviceWaitIdle(m_Device));

    for (FrameData& frame : m_Frames) {
        VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    }
    vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

    for (SwapchainImageData& image : m_Images) {
        vkDestroySemaphore(m_Device, image.renderSemaphore, nullptr);
        vkDestroyImageView(m_Device, image.view, nullptr);
        vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr);
        vkDestroyFramebuffer(m_Device, image.depthFramebuffer, nullptr);
        vkDestroyImageView(m_Device, image.depthView, nullptr);
        vmaDestroyImage(m_Allocator, image.depthImage.image, image.depthImage.allocation);
        vkDestroyImageView(m_Device, image.msaaColorView, nullptr);
        vmaDestroyImage(m_Allocator, image.msaaColorImage.image, image.msaaColorImage.allocation);
        vkDestroyImageView(m_Device, image.resolvedDepthView, nullptr);
        vmaDestroyImage(m_Allocator, image.resolvedDepthImage.image, image.resolvedDepthImage.allocation);
        image.flightFence = VK_NULL_HANDLE;
    }
    for (FrameData& frame : m_Frames) {
        vkDestroyFence(m_Device, frame.renderFence, nullptr);
        vkDestroySemaphore(m_Device, frame.acquireSemaphore, nullptr);
        vmaUnmapMemory(m_Allocator, frame.lightCulling.lightBuffer.allocation);
        vmaDestroyBuffer(m_Allocator, frame.lightCulling.lightBuffer.buffer, frame.lightCulling.lightBuffer.allocation);
        vmaDestroyBuffer(m_Allocator, frame.lightCulling.lightIndices.buffer, frame.lightCulling.lightIndices.allocation);
        vmaDestroyBuffer(m_Allocator, frame.lightCulling.tileCounts.buffer, frame.lightCulling.tileCounts.allocation);
    }

    m_Extent = capabilities.currentExtent;
    m_Viewport.width = static_cast<float>(m_Extent.width);
    m_Viewport.height = static_cast<float>(m_Extent.height);
    m_Scissor.extent = m_Extent;

    m_DynamicDeletionQueue.Clear();

    InitSwapchain();
    InitResolveResources();
    InitLightCullingResources();
    InitFramebuffers();
    InitSyncStructures();

    Camera& cam = ecs.GetComponent<Camera>(m_Camera);
    cam.aspect = static_cast<float>(m_Extent.width) / static_cast<float>(m_Extent.height);
    cam.projection = Camera::VulkanPerspective(cam.fov, cam.aspect, cam.near, cam.far);

    m_DidResize = false;
}

void RenderSystem::InitVulkan(GLFWwindow *window)
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

    m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    INFO("Selected physical device: " << physicalDevice.name);

    VmaAllocatorCreateInfo allocInfo = {
        .physicalDevice = m_PhysicalDevice,
        .device = m_Device,
        .instance = m_Instance,
    };
    vmaCreateAllocator(&allocInfo, &m_Allocator);
}

void RenderSystem::InitMSAA(uint64_t requestedSamples)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
    VkSampleCountFlags available = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;

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

void RenderSystem::InitSwapchain()
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

    m_DynamicDeletionQueue.PushFunction([=, this] { 
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr); 
    });

    VkExtent3D depthExtent = {
        .width = m_Extent.width,
        .height = m_Extent.height,
        .depth = 1
    };

    m_DepthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo depthImageInfo = vkinit::ImageCreateInfo(m_DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, depthExtent);
    depthImageInfo.samples = m_MSAASamples;
    VmaAllocationCreateInfo depthImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    for (SwapchainImageData& image : m_Images) {
        vmaCreateImage(m_Allocator, &depthImageInfo, &depthImageAllocInfo, &image.depthImage.image, &image.depthImage.allocation, nullptr);

        VkImageViewCreateInfo depthViewInfo = vkinit::ImageViewCreateInfo(m_DepthFormat, image.depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
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

void RenderSystem::InitCommands()
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

    VK_CHECK(vkCreateCommandPool(m_Device, &createInfo, nullptr, &m_UploadContext.commandPool));

    VkCommandBufferAllocateInfo alloc_info = vkinit::CommandBufferAllocateInfo(m_UploadContext.commandPool);
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &alloc_info, &m_UploadContext.commandBuffer));

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyCommandPool(m_Device, m_UploadContext.commandPool, nullptr); 
    });
}

void RenderSystem::InitDefaultRenderPass()
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
        .format = m_DepthFormat,
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
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

void RenderSystem::InitFramebuffers()
{
    VkFramebufferCreateInfo info = vkinit::FramebufferCreateInfo(m_RenderPass, m_Extent);
    VkFramebufferCreateInfo depthInfo = vkinit::FramebufferCreateInfo(m_DepthRenderPass, m_Extent);

    for (SwapchainImageData& image : m_Images) {
        VkImageView attachments[3] = { image.msaaColorView, image.view, image.depthView };
        info.attachmentCount = 3;
        info.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(m_Device, &info, nullptr, &image.framebuffer));

        // TODO: Are the depth attachments being passed from the prepass to the main pass correctly?
        depthInfo.attachmentCount = 1;
        depthInfo.pAttachments = &image.depthView;
        VK_CHECK(vkCreateFramebuffer(m_Device, &depthInfo, nullptr, &image.depthFramebuffer));

        m_DynamicDeletionQueue.PushFunction([=, this] { 
            vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr); 
            vkDestroyFramebuffer(m_Device, image.depthFramebuffer, nullptr); 
        });
    }
}

void RenderSystem::InitResolveResources()
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

void RenderSystem::InitLightCulling()
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
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
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

void RenderSystem::InitResolve()
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

void RenderSystem::InitLightCullingResources()
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

void RenderSystem::InitDepthRenderPass()
{
    VkAttachmentDescription depthAttachment = {
        .format = m_DepthFormat,
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

void RenderSystem::InitDepthPipeline()
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

void RenderSystem::InitLightCullingPipeline()
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

void RenderSystem::InitResolvePipeline()
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

void RenderSystem::InitSyncStructures()
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

    if (!m_UploadContext.initialized) {
        fenceInfo.flags = 0;
        VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_UploadContext.uploadFence));

        m_MainDeletionQueue.PushFunction([=, this] {
            vkDestroyFence(m_Device, m_UploadContext.uploadFence, nullptr);
        });

        m_UploadContext.initialized = true;
    }
}

void RenderSystem::InitMainPipelines()
{
    // Build viewport and scissor
    m_Viewport = {
        .x = 0.0,
        .y = 0.0,
        .width = static_cast<float>(m_Extent.width),
        .height = static_cast<float>(m_Extent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };

    m_Scissor = {
        .offset = { 0, 0 },
        .extent = m_Extent
    };

    // Pipeline
    VkPushConstantRange constantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    VkDescriptorSetLayout layouts[3] = { m_DescriptorLayout, m_ArrayDescriptorLayout, m_LightCullingDescriptorLayout };
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

void RenderSystem::InitGlobalDescriptor()
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
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorLayout);

    // Array descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings(m_MaxTextureArrays);
    for (const auto& [index, binding] : bindings | std::ranges::views::enumerate) {
        binding.binding = index;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    layoutInfo.pBindings = bindings.data();
    layoutInfo.bindingCount = m_MaxTextureArrays;
    vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_ArrayDescriptorLayout);
    
    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_Device, m_ArrayDescriptorLayout, nullptr);
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
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_MaxTextureArrays }
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
        allocInfo.pSetLayouts = &m_ArrayDescriptorLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocInfo, &frame.arrayDescriptorSet));
    }
}

void RenderSystem::ImmediateSubmit(std::function<void (VkCommandBuffer commandBuffer)>&& function)
{
    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(m_UploadContext.commandBuffer, &beginInfo));

    function(m_UploadContext.commandBuffer);

    VK_CHECK(vkEndCommandBuffer(m_UploadContext.commandBuffer));

    VkSubmitInfo submitInfo = vkinit::SubmitInfo(&m_UploadContext.commandBuffer);
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_UploadContext.uploadFence));

    VK_CHECK(vkWaitForFences(m_Device, 1, &m_UploadContext.uploadFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_Device, 1, &m_UploadContext.uploadFence));

    VK_CHECK(vkResetCommandPool(m_Device, m_UploadContext.commandPool, 0));
}

void RenderSystem::LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module)
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

uint32_t RenderSystem::CreateTextureArray(uint32_t resolution, uint32_t size, VkFormat format)
{
    TextureArray array;
    array.resolution = resolution;
    array.layerCount = size;
    array.format = format;

    array.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(array.resolution, array.resolution)))) + 1;

    for (uint32_t i = 0; i < array.layerCount; i++) {
        array.freeLayers.push(i);
    }

    VkExtent3D extent = {
        .width = array.resolution,
        .height = array.resolution,
        .depth = 1
    };

    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(array.format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, extent);
    imageInfo.arrayLayers = array.layerCount;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.mipLevels = array.mipLevels;

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };
    vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &array.image.image, &array.image.allocation, nullptr);

    VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(array.format, array.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.subresourceRange.layerCount = array.layerCount;
    viewInfo.subresourceRange.levelCount = array.mipLevels;
    VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &array.view));

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0,
        .minLod = 0.0,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &array.sampler));

    // Transition whole array to shader read
    ImmediateSubmit([=](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = array.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = array.layerCount
        };
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = array.image.image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroySampler(m_Device, array.sampler, nullptr);
        vkDestroyImageView(m_Device, array.view, nullptr);
        vmaDestroyImage(m_Allocator, array.image.image, array.image.allocation);
    });

    uint32_t index = m_TextureArrays.size();
    m_TextureArrays.push_back(array);

    // NOTE: 2 loops because imageInfos needs a lifetime equal to descriptorWrites
    std::vector<VkDescriptorImageInfo> imageInfos(m_TextureArrays.size());
    for (uint32_t i = 0; i < imageInfos.size(); i++) {
        imageInfos[i] = {
            .sampler = m_TextureArrays[i].sampler,
            .imageView = m_TextureArrays[i].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(m_TextureArrays.size());
    for (FrameData& frame : m_Frames) {
        for (uint32_t i = 0; i < descriptorWrites.size(); i++) {
            descriptorWrites[i] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = frame.arrayDescriptorSet,
                .dstBinding = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[i]
            };
        }
        vkUpdateDescriptorSets(m_Device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    return index;
}

uint32_t RenderSystem::AllocateTexture(ImageData *imageData, TextureArray& array)
{
    ASSERT(array.freeLayers.size() > 0 && "Allocating too many texture array layers");
    uint32_t layerIndex = array.freeLayers.front();
    array.freeLayers.pop();

    if (static_cast<uint32_t>(imageData->width) < array.resolution || static_cast<uint32_t>(imageData->width) < array.resolution) {
        imageData->Resize(array.resolution, array.resolution);
    }

    ASSERT(imageData->width == static_cast<int>(array.resolution) && "Image width doesn't match array texture");
    ASSERT(imageData->height == static_cast<int>(array.resolution) && "Image height doesn't match array texture");
    ASSERT(imageData->channels == 4 && "Image has incorrect number of channels");

    VkDeviceSize imageSize = imageData->width * imageData->height * imageData->channels;

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    VK_CHECK(vmaCreateBuffer(m_Allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

    vmaMapMemory(m_Allocator, stagingBuffer.allocation, &stagingBuffer.data);
    memcpy(stagingBuffer.data, imageData->pixels, imageSize);
    vmaUnmapMemory(m_Allocator, stagingBuffer.allocation);

    // Clean up image data
    stbi_image_free(imageData->pixels);

    // Copy buffer to texture array gpu memory
    ImmediateSubmit([=, this](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = layerIndex,
            .layerCount = 1
        };

        // Transition image to optimal layout for a data transfer
        VkImageMemoryBarrier barrierToTransfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = array.image.image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

        // Copy staging buffer to image
        VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            },
            .imageExtent = { array.resolution, array.resolution, 1 }
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, array.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Check for linear blit support (for mipmap creation)
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, array.format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            ERROR("Texture image format does not support linear blitting");
            WARN("TODO: Implement alternative image resizing for mipmap generation");
            abort();
        }

        // Generate mipmaps
        VkImageMemoryBarrier mipmapBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = array.image.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .baseArrayLayer = layerIndex,
                .layerCount = 1,
            },
        };

        // Transfer mip level 0 to transfer src
        mipmapBarrier.subresourceRange.baseMipLevel = 0;
        mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);

        // Transfer all mip levels except level 0 to transfer destination
        for (uint32_t i = 1; i < array.mipLevels; i++) {
            mipmapBarrier.subresourceRange.baseMipLevel = i;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
        }
        
        int32_t mipWidth = array.resolution;
        int32_t mipHeight = array.resolution;
        for (uint32_t i = 1; i < array.mipLevels; i++) {
            // Blit next mip level
            VkImageBlit blit = {
                .srcSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i - 1,
                    .baseArrayLayer = layerIndex,
                    .layerCount = 1
                },
                .srcOffsets = {
                    { 0, 0, 0 },
                    { mipWidth, mipHeight, 1 }
                },
                .dstSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = i,
                    .baseArrayLayer = layerIndex,
                    .layerCount = 1
                },
                .dstOffsets = {
                    { 0, 0, 0 },
                    { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 }
                },
            };
            vkCmdBlitImage(commandBuffer, array.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, array.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            // Wait for blit to finish and transfer to shader-readable
            mipmapBarrier.subresourceRange.baseMipLevel = i - 1;
            mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);

            // If not last mip level, transition current level to src, ready to blit to next
            if (i < array.mipLevels - 1) {
                mipmapBarrier.subresourceRange.baseMipLevel = i;
                mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mipmapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
            }

            // Update dimensions for next level
            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // Transition final mip level to shader-readable
        mipmapBarrier.subresourceRange.baseMipLevel = array.mipLevels - 1;
        mipmapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mipmapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        mipmapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        mipmapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipmapBarrier);
    });

    vmaDestroyBuffer(m_Allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    return layerIndex;
}
