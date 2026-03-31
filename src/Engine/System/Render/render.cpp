#include "render.h"

#include <cmath>
#include <fstream>

#include "Engine/Component/camera.h"
#include "Engine/System/Render/initialiser.h"
#include "Engine/System/Render/pipeline.h"
#include "Engine/Util/allocator.h"
#include "Engine/Util/logger.h"
#include "Engine/Component/transform.h"
#include "Engine/Component/material.h"

#include <VkBootstrap/VkBootstrap.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

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

void RenderSystem::Init(ECS& ecs, GLFWwindow *window, Config *config)
{
    m_Camera = ecs.CreateEntity();
    Camera camera = Camera(glm::vec3(0.0), glm::vec2(config->windowWidth, config->windowHeight), glm::radians(60.0), 0.1, 1000.0);
    ecs.AddComponent<Camera>(m_Camera, camera);

    m_Extent.width = config->windowWidth;
    m_Extent.height = config->windowHeight;

    switch (config->syncStrategy) {
        case SYNC_STRATEGY_UNCAPPED: 
            m_PresentMode = VK_PRESENT_MODE_MAILBOX_KHR; 
            break;
        case SYNC_STRATEGY_VSYNC: 
            m_PresentMode = VK_PRESENT_MODE_FIFO_KHR; 
            break;
        default: 
            ERROR("Invalid sync strategy: " << config->syncStrategy); 
            abort();
    };
    
    InitVulkan(window);
    InitSwapchain();
    InitCommands();
    InitDefaultRenderPass();
    InitFramebuffers();
    InitSyncStructures();
    InitPipelines();
    InitUniformBuffers();

    m_Initialized = true;
}

void RenderSystem::Cleanup() 
{
    if (m_Initialized) {
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

    FrameData &frame = m_Frames[m_FrameNum % FRAMES_IN_FLIGHT];

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

    ImageData &image = m_Images[imageIndex];

    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));

    if (image.flightFence != VK_NULL_HANDLE && image.flightFence != frame.renderFence) {
        VK_CHECK(vkWaitForFences(m_Device, 1, &image.flightFence, VK_TRUE, UINT64_MAX));
    }
    image.flightFence = frame.renderFence;

    VkCommandBufferBeginInfo beginInfo = vkinit::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    VkClearValue colorClear = { .color = {{ 0.5, 0.7, 1.0, 1.0 }} };
    VkClearValue depthClear = { .depthStencil = { .depth = 1.0f } };
    VkClearValue clearValues[2] = { colorClear, depthClear };

    VkRenderPassBeginInfo passInfo = vkinit::RenderPassBeginInfo(m_RenderPass, image.framebuffer, &clearValues[0], m_Extent);
    vkCmdBeginRenderPass(frame.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &m_Scissor);

    Camera& cam = ecs.GetComponent<Camera>(m_Camera);

    for (Entity e : entities) {
        Mesh& mesh = ecs.GetComponent<Mesh>(e);
        Transform& transform = ecs.GetComponent<Transform>(e);
        Material& material = ecs.GetComponent<Material>(e);

        VERIFY(mesh.allocated && "Drawing unallocated mesh");

        vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);

        material.defaultConstants.mvp = cam.projection * cam.view * Transform::ToModelMatrix(transform);
        vkCmdPushConstants(frame.commandBuffer, material.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MaterialPushConstants), &material.defaultConstants.mvp);

        // TODO: For the uniforms, can have an array of bytes (up to some maximum size)
        //       Each uniform that is added will then be broken down into the bytes 
        //       for easy pushing to the frame uniform buffer.
        //       Also track the size of the buffer
        // TODO: Uniforms
        // if (material.vertexUniforms.size > 0) {
        //     memcpy(frame.uniformBuffer.data.ptr, &material.vertexUniforms.ptr, material.vertexUniforms.size);
        //
        //     VkDescriptorBufferInfo bufferInfo = {
        //         .buffer = frame.uniformBuffer.buffer,
        //         .offset = 0,
        //         .range = material.vertexUniforms.size,
        //     };
        //     VkWriteDescriptorSet descriptorWrite = {
        //         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //         .pNext = nullptr,
        //         .dstSet = frame.descriptorSets[material.index],
        //         .dstBinding = 0,
        //         .dstArrayElement = 0,
        //         .descriptorCount = 1,
        //         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        //         .pBufferInfo = &bufferInfo,
        //     };
        //     vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);
        //     vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipelineLayout, 0, 1, &frame.descriptorSets[material.index], 0, nullptr);
        // }

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
    VkBufferCreateInfo vertexBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = mesh.vertexCount * sizeof(Vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    VK_CHECK(vmaCreateBuffer(m_Allocator, &vertexBufferInfo, &allocInfo, &mesh.vertexBuffer.buffer, &mesh.vertexBuffer.allocation, nullptr));

    void *data;
    vmaMapMemory(m_Allocator, mesh.vertexBuffer.allocation, &data);
    memcpy(data, mesh.vertices, mesh.vertexCount * sizeof(Vertex));
    vmaUnmapMemory(m_Allocator, mesh.vertexBuffer.allocation);

    delete[] mesh.vertices;
    mesh.allocated = true;

    m_MainDeletionQueue.PushFunction([=, this] {
        vmaDestroyBuffer(m_Allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
    });
}

Material RenderSystem::CreateMaterial(const MaterialInfo *info)
{
    VERIFY(m_AllocatedMaterials < MAX_MATERIALS && "Creating too many materials");

    Material mat;
    mat.index = m_AllocatedMaterials++;

    VkDescriptorSetLayoutCreateInfo descriptorInfo = vkinit::DescriptorSetLayoutCreateInfo();
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &descriptorInfo, nullptr, &mat.descriptorLayout));

    m_MainDeletionQueue.PushFunction([=, this] { vkDestroyDescriptorSetLayout(m_Device, mat.descriptorLayout, nullptr); });

    mat.defaultConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(MaterialPushConstants),
    };

    VkPipelineLayoutCreateInfo pipelineInfo = vkinit::LayoutCreateInfo(&mat.descriptorLayout, &mat.defaultConstantRange);
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineInfo, nullptr, &mat.pipelineLayout)); 

    m_MainDeletionQueue.PushFunction([=, this] { vkDestroyPipelineLayout(m_Device, mat.pipelineLayout, nullptr); });

    // Load modules
    VkShaderModule vert;
    LoadShaderModule(info->vertexShader, &vert);

    VkShaderModule frag;
    LoadShaderModule(info->fragmentShader, &frag);

    // Build pipeline
    PipelineBuilder builder;
    VertexInputDesc inputDesc = Vertex::GetVertexDesc();
    builder.SetVertexInput(vkinit::VertexInputStateCreateInfo(&inputDesc.bindings, &inputDesc.attributes))
           .SetInputAssembly(vkinit::InputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
           .SetRasterizer(vkinit::RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL))
           .SetMultisampling(vkinit::MultisampleStateCreateInfo())
           .SetColorBlend(vkinit::ColorBlendAttachmentState())
           .SetDepthStencil(vkinit::DepthStencilStateCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL))
           .SetPipelineLayout(mat.pipelineLayout);

    builder.MakeShader(vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert),
                       vkinit::ShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag));
    mat.pipeline = builder.BuildPipeline(m_Device, m_RenderPass, m_Viewport, m_Scissor);

    m_MainDeletionQueue.PushFunction([=, this] { vkDestroyPipeline(m_Device, mat.pipeline, nullptr); });

    vkDestroyShaderModule(m_Device, vert, nullptr);
    vkDestroyShaderModule(m_Device, frag, nullptr);

    // Allocate descriptor sets from each frame's pool 
    for (FrameData& frame : m_Frames) {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = frame.descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mat.descriptorLayout
        };
        vkAllocateDescriptorSets(m_Device, &allocInfo, &frame.descriptorSets[mat.index]);

        m_MainDeletionQueue.PushFunction([=, this] { vkFreeDescriptorSets(m_Device, frame.descriptorPool, 1, &frame.descriptorSets[mat.index]); });
    }

    return mat;
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

    for (ImageData& image : m_Images) {
        vkDestroySemaphore(m_Device, image.renderSemaphore, nullptr);
        vkDestroyImageView(m_Device, image.view, nullptr);
        vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr);
        vkDestroyImageView(m_Device, image.depthView, nullptr);
        vmaDestroyImage(m_Allocator, image.depthImage.image, image.depthImage.allocation);
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

    vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1)
                                                 .set_surface(m_Surface)
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
    m_Images = std::vector<ImageData>(swapchainImages.size());
    for (size_t i = 0; i < m_Images.size(); i++) {
        m_Images[i].image = swapchainImages[i];
        m_Images[i].view = swapchainViews[i];

        m_DynamicDeletionQueue.PushFunction([=, this] { vkDestroyImageView(m_Device, m_Images[i].view, nullptr); });
    }

    m_DynamicDeletionQueue.PushFunction([=, this] { vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr); });

    // TODO: Resize the depth image
    VkExtent3D depthExtent = {
        .width = m_Extent.width,
        .height = m_Extent.height,
        .depth = 1
    };

    m_DepthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo depthImageInfo = vkinit::ImageCreateInfo(m_DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthExtent);
    VmaAllocationCreateInfo depthImageAllocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    for (ImageData& image : m_Images) {
        VK_CHECK(vmaCreateImage(m_Allocator, &depthImageInfo, &depthImageAllocInfo, &image.depthImage.image, &image.depthImage.allocation, nullptr));

        VkImageViewCreateInfo depthViewInfo = vkinit::ImageViewCreateInfo(m_DepthFormat, image.depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK(vkCreateImageView(m_Device, &depthViewInfo, nullptr, &image.depthView));

        m_DynamicDeletionQueue.PushFunction([=, this] {
            vkDestroyImageView(m_Device, image.depthView, nullptr);
            vmaDestroyImage(m_Allocator, image.depthImage.image, image.depthImage.allocation);
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

        m_MainDeletionQueue.PushFunction([=, this] { vkDestroyCommandPool(m_Device, frame.commandPool, nullptr); });
    }
}

void RenderSystem::InitDefaultRenderPass()
{
    VkAttachmentDescription colorAttachment = {
        .format = m_SwapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription depthAttachment = {
        .format = m_DepthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depthRef = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
        .pDepthStencilAttachment = &depthRef
    };

    VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = &attachments[0],
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    // TODO: Subpass dependencies?

    VK_CHECK(vkCreateRenderPass(m_Device, &info, nullptr, &m_RenderPass));

    m_MainDeletionQueue.PushFunction([=, this] { vkDestroyRenderPass(m_Device, m_RenderPass, nullptr); });
}

void RenderSystem::InitFramebuffers()
{
    VkFramebufferCreateInfo info = vkinit::FramebufferCreateInfo(m_RenderPass, m_Extent);

    for (ImageData& image : m_Images) {
        VkImageView attachments[2] = { image.view, image.depthView };
        info.attachmentCount = 2;
        info.pAttachments = &attachments[0];
        VK_CHECK(vkCreateFramebuffer(m_Device, &info, nullptr, &image.framebuffer));

        m_DynamicDeletionQueue.PushFunction([=, this] { vkDestroyFramebuffer(m_Device, image.framebuffer, nullptr); });
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

    for (ImageData& image : m_Images) {
        image.flightFence = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &image.renderSemaphore));

        m_DynamicDeletionQueue.PushFunction([=, this] { vkDestroySemaphore(m_Device, image.renderSemaphore, nullptr); });
    }
}

void RenderSystem::InitPipelines()
{
    for (FrameData& frame : m_Frames) {
        VkDescriptorPoolSize size = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = MAX_DESCRIPTORS
        };
        VkDescriptorPoolCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = MAX_MATERIALS,
            .poolSizeCount = 1,
            .pPoolSizes = &size,
        };
        VK_CHECK(vkCreateDescriptorPool(m_Device, &createInfo, nullptr, &frame.descriptorPool));

        m_MainDeletionQueue.PushFunction([=, this] { vkDestroyDescriptorPool(m_Device, frame.descriptorPool, nullptr); });
    }

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
}

void RenderSystem::InitUniformBuffers()
{
    constexpr size_t MAX_UNIFORM_BUFFER_SIZE = 16 * 1024; // 16 KiB
    VkBufferCreateInfo uniformBufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = MAX_UNIFORM_BUFFER_SIZE,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };
    for (FrameData &frame : m_Frames) {
        VmaAllocationCreateInfo allocInfo = {
            .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        };
        VK_CHECK(vmaCreateBuffer(m_Allocator, &uniformBufferInfo, &allocInfo, &frame.uniformBuffer.buffer, &frame.uniformBuffer.allocation, nullptr));

        // NOTE: Persistent mapping for app's lifetime.
        vmaMapMemory(m_Allocator, frame.uniformBuffer.allocation, &frame.uniformBuffer.data);

        m_MainDeletionQueue.PushFunction([=, this] {
            vmaUnmapMemory(m_Allocator, frame.uniformBuffer.allocation);
            vmaDestroyBuffer(m_Allocator, frame.uniformBuffer.buffer, frame.uniformBuffer.allocation);
        });
    }
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
