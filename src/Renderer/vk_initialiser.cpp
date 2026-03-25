#include "vk_initialiser.h"

VkCommandBufferBeginInfo vkinit::CommandBufferBeginInfo(VkCommandBufferUsageFlags flags)
{
    return (VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = flags,
        .pInheritanceInfo = nullptr,
    };
}

VkRenderPassBeginInfo vkinit::RenderPassBeginInfo(VkRenderPass pass, VkFramebuffer framebuffer, VkClearValue *clearVal, VkExtent2D extent)
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
        .clearValueCount = 1,
        .pClearValues = clearVal,
    };
}

VkSubmitInfo vkinit::SubmitInfo(FrameData *frame, ImageData *image, VkPipelineStageFlags *waitStage)
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

VkPresentInfoKHR vkinit::PresentInfo(ImageData *image, VkSwapchainKHR *swapchain, uint32_t *imageIndex)
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

VkPipelineShaderStageCreateInfo vkinit::ShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule module)
{
    return (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = stage,
        .module = module,
        .pName = "main" // This means that the entrypoint to the shader functions is "main"
    };
}

VkPipelineVertexInputStateCreateInfo vkinit::VertexInputStateCreateInfo()
{
    return (VkPipelineVertexInputStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0
    };
}

VkPipelineInputAssemblyStateCreateInfo vkinit::InputAssemblyStateCreateInfo(VkPrimitiveTopology topology)
{
    return (VkPipelineInputAssemblyStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .topology = topology,
        .primitiveRestartEnable = VK_FALSE
    };
}

// NOTE: Polygon mode here lets us toggle between things like wireframe and solid
VkPipelineRasterizationStateCreateInfo vkinit::RasterizationStateCreateInfo(VkPolygonMode polygonMode)
{
    return (VkPipelineRasterizationStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = polygonMode,
        // Backface culling
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        // No depth bias
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0, 
        .depthBiasClamp = 0.0,
        .depthBiasSlopeFactor = 0.0,
        .lineWidth = 1.0    
    };
}

VkPipelineMultisampleStateCreateInfo vkinit::MultisampleStateCreateInfo()
{
    return (VkPipelineMultisampleStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        // No multisampling (for MSAA, we need to support it in the renderpass too)
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
}

VkPipelineColorBlendAttachmentState vkinit::ColorBlendAttachmentState()
{
    return (VkPipelineColorBlendAttachmentState) {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
}

VkPipelineLayoutCreateInfo vkinit::LayoutCreateInfo()
{
    return (VkPipelineLayoutCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        // Shader has no inputs right now, so these are both null
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
}
