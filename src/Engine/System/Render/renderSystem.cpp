#include "renderSystem.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "Component/camera.h"
#include "Geometry/frustum.h"
#include "System/Render/initialiser.h"
#include "System/Render/pipeline.h"
#include "System/Render/renderTypes.h"
#include "Util/allocator.h"
#include "Util/logger.h"
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

    InitVulkan(window);
    InitMSAA(config->msaaSamples);
    InitSwapchain();
    InitCommands();
    InitDefaultRenderPass();
    InitFramebuffers();
    InitSyncStructures();
    InitUniformBuffers();
    InitPipelines();

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

void RenderSystem::Draw(ECS& ecs)
{
    if (m_DidResize) {
        Resize(ecs);
        m_DidResize = false;
    }

    size_t frameIndex = m_FrameNum % FRAMES_IN_FLIGHT;
    FrameData &frame = m_Frames[frameIndex];
    frame.descriptorOffset = 0; // Reset descriptor offset, ready to copy in from material uniforms

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

    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    VkClearValue colorClear = { .color = {{ 0.5, 0.7, 1.0, 1.0 }} };
    VkClearValue resolveClear = { .color = {{ 0.0, 0.0, 0.0, 0.0 }} };
    VkClearValue depthClear = { .depthStencil = { .depth = 1.0f } };
    VkClearValue clearValues[3] = { colorClear, resolveClear, depthClear };

    VkRenderPassBeginInfo passInfo = vkinit::RenderPassBeginInfo(m_RenderPass, image.framebuffer, &clearValues[0], 3, m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_Scissor);

    Camera& cam = ecs.GetComponent<Camera>(m_Camera);
    Transform& camTransform = ecs.GetComponent<Transform>(m_Camera);

    glm::mat4 vp = cam.projection * cam.view;
    Frustum camFrustum = Frustum(vp);

    for (Entity e : entities) {
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);

        glm::mat4x4 model = transform.ModelMatrix();
        if (transform.inherit != INVALID_HANDLE) {
            model = ecs.GetComponent<Transform>(transform.inherit).ModelMatrix() * model;
        }

        // Frustum culling
        CentreExtents centreExtents = CentreExtents(mesh.bounds);
        if (!camFrustum.Intersects(centreExtents.WorldSpace(model))) {
            continue; 
        }

        ASSERT(mesh.allocated && "Drawing unallocated mesh");

        Material& material = ecs.GetComponent<Material>(e);
        if (material.hasTransparency) {
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TransparencyPipeline);
        } else {
            vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
        }

        PushConstants constants = {
            .mvp = vp * model,
            .model = model
        };
        vkCmdPushConstants(frame.commandBuffer, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

        uint32_t vertexOffset = frame.descriptorOffset;
        frame.descriptorOffset += Align(sizeof(VertexUniforms));

        uint32_t fragmentOffset = frame.descriptorOffset;
         frame.descriptorOffset += Align(sizeof(FragmentUniforms));

        ASSERT(frame.descriptorOffset <= MAX_UNIFORM_BUFFER_SIZE && "Uniform buffer overflow");

        VertexUniforms vertexUniforms = {
            .placeholder = glm::vec4(0.0)
        };
        FragmentUniforms fragmentUniforms = {
            .lightPos = glm::vec4(0.0),
            .viewPos = glm::vec4(camTransform.translation, 0.0),
        };
        memcpy(static_cast<char *>(frame.uniformBuffer.data) + vertexOffset, &vertexUniforms, sizeof(VertexUniforms));
        memcpy(static_cast<char *>(frame.uniformBuffer.data) + fragmentOffset, &fragmentUniforms, sizeof(FragmentUniforms));

        uint32_t dynamicOffsets[2] = { vertexOffset, fragmentOffset };
        vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &material.descriptorSets[frameIndex], 2, dynamicOffsets);

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

    VK_CHECK(vmaMapMemory(m_Allocator, stagingBuffer.allocation, &stagingBuffer.data));
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

AllocatedImage RenderSystem::AllocateImage(ImageData& imageData)
{
    void *pixelPtr = imageData.pixels;
    VkDeviceSize imageSize = imageData.width * imageData.height * 4; // rgba = 4
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB; // 32 bits, 8 bpc

    // Staging buffer
    VkBufferCreateInfo stagingBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT // Only for data transfer, not rendering
    };
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY
    };

    AllocatedBuffer stagingBuffer;
    VK_CHECK(vmaCreateBuffer(m_Allocator, &stagingBufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.allocation, nullptr));

    VK_CHECK(vmaMapMemory(m_Allocator, stagingBuffer.allocation, &stagingBuffer.data));
    memcpy(stagingBuffer.data, pixelPtr, imageSize);
    vmaUnmapMemory(m_Allocator, stagingBuffer.allocation);

    // Clean up image data
    stbi_image_free(imageData.pixels);

    // Allocate the GPU image
    VkExtent3D imageExtent = {
        .width = static_cast<uint32_t>(imageData.width),
        .height = static_cast<uint32_t>(imageData.height),
        .depth = 1
    };

    VkImageCreateInfo imageInfo = vkinit::ImageCreateInfo(imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);
    VmaAllocationCreateInfo imageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };

    AllocatedImage image;
    VK_CHECK(vmaCreateImage(m_Allocator, &imageInfo, &imageAllocInfo, &image.image, &image.allocation, nullptr));

    // Copy buffer to GPU
    ImmediateSubmit([=](VkCommandBuffer commandBuffer) {
        VkImageSubresourceRange range = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };
        VkImageMemoryBarrier barrierToTransfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image.image,
            .subresourceRange = range,
        };
        // Prepare the image for a memory transfer and block the pipeline until the transfer is complete
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

        // Copy staging buffer to image
        VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageExtent = imageExtent
        };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier barrierToReadable = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = image.image,
            .subresourceRange = range,
        };
        // Change image layout back to be readable by shaders
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToReadable);
    });

    // Cleanup
    m_MainDeletionQueue.PushFunction([=, this] {
        vmaDestroyImage(m_Allocator, image.image, image.allocation);
    });

    vmaDestroyBuffer(m_Allocator, stagingBuffer.buffer, stagingBuffer.allocation);

    return image;
}

void RenderSystem::CreateMaterial(const MaterialInfo& info, Material& mat)
{
    // Texture
    mat.textureImage = info.textureImage;
    mat.hasTransparency = info.hasTransparency;
    VkImageViewCreateInfo viewInfo = vkinit::ImageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, mat.textureImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &mat.textureView));

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT
    };
    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &mat.textureSampler));

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyImageView(m_Device, mat.textureView, nullptr);
        vkDestroySampler(m_Device, mat.textureSampler, nullptr);
    });
    
    for (const auto& [index, frame] : m_Frames | std::ranges::views::enumerate) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = frame.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_MaterialSetLayout,
        };
        VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocInfo, &mat.descriptorSets[index]));
        VkDescriptorBufferInfo vertexBufferInfo = {
            .buffer = frame.uniformBuffer.buffer,
            .offset = 0,
            .range = sizeof(VertexUniforms),
        };
        VkDescriptorBufferInfo fragmentBufferInfo = {
            .buffer = frame.uniformBuffer.buffer,
            .offset = 0,
            .range = sizeof(FragmentUniforms),
        };
        VkDescriptorImageInfo imageInfo = {
            .sampler = mat.textureSampler,
            .imageView = mat.textureView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet writes[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = mat.descriptorSets[index],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .pBufferInfo = &vertexBufferInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = mat.descriptorSets[index],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .pBufferInfo = &fragmentBufferInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = mat.descriptorSets[index],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfo
            }
        };
        vkUpdateDescriptorSets(m_Device, 3, &writes[0], 0, nullptr);
    }
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
        vkDestroyImageView(m_Device, image.depthView, nullptr);
        vmaDestroyImage(m_Allocator, image.depthImage.image, image.depthImage.allocation);
        vkDestroyImageView(m_Device, image.msaaColorView, nullptr);
        vmaDestroyImage(m_Allocator, image.msaaColorImage.image, image.msaaColorImage.allocation);
        image.flightFence = VK_NULL_HANDLE;
    }
    for (FrameData& frame : m_Frames) {
        vkDestroyFence(m_Device, frame.renderFence, nullptr);
        vkDestroySemaphore(m_Device, frame.acquireSemaphore, nullptr);
    }

    m_Extent = capabilities.currentExtent;
    m_Viewport.width = static_cast<float>(m_Extent.width);
    m_Viewport.height = static_cast<float>(m_Extent.height);
    m_Scissor.extent = m_Extent;

    m_DynamicDeletionQueue.Clear();

    InitSwapchain();
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
    VK_CHECK(vmaCreateAllocator(&allocInfo, &m_Allocator));
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
    if (m_MSAASamples > requestedSamples) {
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

    VkImageCreateInfo depthImageInfo = vkinit::ImageCreateInfo(m_DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthExtent);
    depthImageInfo.samples = m_MSAASamples;
    VmaAllocationCreateInfo depthImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    for (SwapchainImageData& image : m_Images) {
        VK_CHECK(vmaCreateImage(m_Allocator, &depthImageInfo, &depthImageAllocInfo, &image.depthImage.image, &image.depthImage.allocation, nullptr));

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
        VK_CHECK(vmaCreateImage(m_Allocator, &msaaImageInfo, &msaaImageAllocInfo, &image.msaaColorImage.image, &image.msaaColorImage.allocation, nullptr));

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
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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

    for (SwapchainImageData& image : m_Images) {
        // TODO
        VkImageView attachments[3] = { image.msaaColorView, image.view, image.depthView };
        info.attachmentCount = 3;
        info.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(m_Device, &info, nullptr, &image.framebuffer));

        m_DynamicDeletionQueue.PushFunction([=, this] { 
            vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr); 
        });
    }
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

void RenderSystem::InitPipelines()
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

    // Push constants
    m_PushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    // Pipeline
    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&m_PushConstantRange);
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &m_MaterialSetLayout;
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &m_PipelineLayout)); 

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr); 
    });

    PipelineBuilder builder;

    VkShaderModule vert, frag;
    LoadShaderModule("src/Engine/Resource/Shader/basic.vert.spirv", &vert);
    LoadShaderModule("src/Engine/Resource/Shader/basic.frag.spirv", &frag);

    builder.MakeShader(vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert),
                       vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag));

    VertexInputDesc inputDesc = Vertex::GetVertexDesc();
    builder.SetVertexInput(vkinit::VertexInputStateCreateInfo(&inputDesc.bindings, &inputDesc.attributes))
           .SetInputAssembly(vkinit::InputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
           .SetRasterizer(vkinit::RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL))
           .SetMultisampling(vkinit::MultisampleStateCreateInfo(m_MSAASamples, false))
           .SetColorBlend(vkinit::ColorBlendAttachmentState())
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL))
           .SetPipelineLayout(m_PipelineLayout);

    m_Pipeline = builder.BuildPipeline(m_Device, m_RenderPass, m_Viewport, m_Scissor);

    builder.SetMultisampling(vkinit::MultisampleStateCreateInfo(m_MSAASamples, true));
    m_TransparencyPipeline = builder.BuildPipeline(m_Device, m_RenderPass, m_Viewport, m_Scissor);

    vkDestroyShaderModule(m_Device, vert, nullptr);
    vkDestroyShaderModule(m_Device, frag, nullptr);

    m_MainDeletionQueue.PushFunction([=, this] { 
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr); 
        vkDestroyPipeline(m_Device, m_TransparencyPipeline, nullptr); 
    });
}

void RenderSystem::InitUniformBuffers()
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);
    m_MinimumUniformOffset = properties.limits.minUniformBufferOffsetAlignment;

    // Per frame uniform buffers and descriptor pools
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = MAX_UNIFORM_BUFFER_SIZE,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };
    VmaAllocationCreateInfo allocInfo = {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    // 2 uniform buffers + 1 sampler per material
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MAX_MATERIALS * 2},
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_MATERIALS }
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .maxSets = MAX_MATERIALS,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes,
    };

    for (FrameData& frame : m_Frames) {
        VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr));

        frame.descriptorOffset = 0;
        VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &frame.descriptorPool));

        VK_CHECK(vmaMapMemory(m_Allocator, frame.uniformBuffer.allocation, &frame.uniformBuffer.data));

        m_MainDeletionQueue.PushFunction([=, this] {
            vmaUnmapMemory(m_Allocator, frame.uniformBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);
            vkDestroyDescriptorPool(m_Device, frame.descriptorPool, nullptr); 
        });
    }

    // Material descriptor set layout
    VkDescriptorSetLayoutBinding bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 3,
        .pBindings = bindings
    };
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_MaterialSetLayout));

    m_MainDeletionQueue.PushFunction([=, this] {
        vkDestroyDescriptorSetLayout(m_Device, m_MaterialSetLayout, nullptr);
    });
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

void RenderSystem::LoadShaderModule(const std::filesystem::path& path, VkShaderModule *module)
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

    *module = shaderModule;
}

size_t RenderSystem::Align(size_t size) {
    return (size + m_MinimumUniformOffset - 1) & ~(m_MinimumUniformOffset - 1);
};
