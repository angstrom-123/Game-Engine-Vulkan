#include "initialiser.h"

VkCommandBufferBeginInfo 
vkinit::CommandBufferBeginInfo(VkCommandBufferUsageFlags flags)
{
    return (VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = flags,
        .pInheritanceInfo = nullptr,
    };
}

VkRenderPassBeginInfo 
vkinit::RenderPassBeginInfo(VkRenderPass pass, VkFramebuffer framebuffer, VkClearValue *clearValues, uint32_t clearValueCount, VkExtent2D extent)
{
    VkRect2D fullRect = {
        .offset = {0}, 
        .extent = extent 
    };
    return (VkRenderPassBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = pass,
        .framebuffer = framebuffer,
        .renderArea = fullRect,
        .clearValueCount = clearValueCount,
        .pClearValues = clearValues,
    };
}

VkFramebufferCreateInfo 
vkinit::FramebufferCreateInfo(VkRenderPass pass, VkExtent2D extent)
{
    return (VkFramebufferCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .renderPass = pass,
        .attachmentCount = 1,
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    };
}

VkSubmitInfo 
vkinit::SubmitInfo(FrameData *frame, SwapchainImageData *image, VkPipelineStageFlags *waitStage)
{
    return (VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->acquireSemaphore,
        .pWaitDstStageMask = waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &image->renderSemaphore,
    };
}

VkSubmitInfo 
vkinit::SubmitInfo(VkCommandBuffer *commandBuffer)
{
    return (VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
}

VkPresentInfoKHR 
vkinit::PresentInfo(SwapchainImageData *image, VkSwapchainKHR *swapchain, uint32_t *imageIndex)
{
    return (VkPresentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &image->renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = swapchain,
        .pImageIndices = imageIndex,
    };
}

VkSemaphoreCreateInfo 
vkinit::SemaphoreCreateInfo(VkSemaphoreCreateFlags flags) 
{
    return (VkSemaphoreCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
    };
}

VkFenceCreateInfo 
vkinit::FenceCreateInfo(VkFenceCreateFlags flags)
{
    return (VkFenceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
    };
}

VkCommandPoolCreateInfo 
vkinit::CommandPoolCreateInfo(uint32_t queueFamilyIndex)
{
    return (VkCommandPoolCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex
    };
}

VkCommandBufferAllocateInfo 
vkinit::CommandBufferAllocateInfo(VkCommandPool pool)
{
    return (VkCommandBufferAllocateInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
}

VkPipelineShaderStageCreateInfo 
vkinit::ShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule module)
{
    return (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stage,
        .module = module,
        .pName = "main" // This means that the entrypoint to the shader functions is "main"
    };
}

VkPipelineVertexInputStateCreateInfo 
vkinit::VertexInputStateCreateInfo(std::vector<VkVertexInputBindingDescription> *bindings, 
                                   std::vector<VkVertexInputAttributeDescription> *attributes)
{
    return (VkPipelineVertexInputStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .vertexBindingDescriptionCount = (bindings) ? static_cast<uint32_t>(bindings->size()) : 0,
        .pVertexBindingDescriptions = (bindings) ? bindings->data() : nullptr,
        .vertexAttributeDescriptionCount = (attributes) ? static_cast<uint32_t>(attributes->size()) : 0,
        .pVertexAttributeDescriptions = (attributes) ? attributes->data() : nullptr,
    };
}

VkPipelineInputAssemblyStateCreateInfo 
vkinit::InputAssemblyStateCreateInfo(VkPrimitiveTopology topology)
{
    return (VkPipelineInputAssemblyStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .topology = topology,
        .primitiveRestartEnable = VK_FALSE
    };
}

// NOTE: Polygon mode here lets us toggle between things like wireframe and solid
VkPipelineRasterizationStateCreateInfo 
vkinit::RasterizationStateCreateInfo(VkPolygonMode polygonMode)
{
    return (VkPipelineRasterizationStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = polygonMode,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0, 
        .depthBiasClamp = 0.0,
        .depthBiasSlopeFactor = 0.0,
        .lineWidth = 1.0    
    };
}

VkPipelineMultisampleStateCreateInfo 
vkinit::MultisampleStateCreateInfo(VkSampleCountFlagBits samples, bool enableA2C)
{
    return (VkPipelineMultisampleStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .rasterizationSamples = samples,
        .sampleShadingEnable = (enableA2C) ? VK_TRUE : VK_FALSE,
        .minSampleShading = 1.0,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = (enableA2C) ? VK_TRUE : VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
}

VkPipelineColorBlendAttachmentState 
vkinit::ColorBlendAttachmentState()
{
    return (VkPipelineColorBlendAttachmentState) {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT
    };
}

VkPipelineLayoutCreateInfo 
vkinit::LayoutCreateInfo(VkPushConstantRange *pushConstant)
{
    return (VkPipelineLayoutCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pushConstantRangeCount = (pushConstant) ? 1u : 0u,
        .pPushConstantRanges = pushConstant
    };
}

VkDescriptorSetLayoutCreateInfo 
vkinit::DescriptorSetLayoutCreateInfo(uint32_t binding, VkDescriptorType type, VkShaderStageFlags flags)
{
    VkDescriptorSetLayoutBinding layoutBinding = {
        .binding = binding,
        .descriptorType = type,
        .descriptorCount = 1,
        .stageFlags = flags,
        .pImmutableSamplers = nullptr
    };

    return (VkDescriptorSetLayoutCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = 1,
        .pBindings = &layoutBinding,
    };
}

VkImageCreateInfo 
vkinit::ImageCreateInfo(VkFormat format, VkImageUsageFlags usage, VkExtent3D extent)
{
    return (VkImageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        // NOTE: this needs to be TILING_LINEAR to read/write from CPU (but it's a lot slower)
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage
    };
}

VkImageViewCreateInfo 
vkinit::ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspect)
{
    return (VkImageViewCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    };
}

VkPipelineDepthStencilStateCreateInfo
vkinit::DepthStencilStateCreateInfo(bool depthTest, bool depthWrite, VkCompareOp compare)
{
    return (VkPipelineDepthStencilStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .depthTestEnable = depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = depthTest ? compare : VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .minDepthBounds = 0.0,
        .maxDepthBounds = 1.0,
    };
}
