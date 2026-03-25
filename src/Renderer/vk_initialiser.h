#pragma once

#include "vk_engine.h"

namespace vkinit {
    VkCommandBufferBeginInfo               CommandBufferBeginInfo(VkCommandBufferUsageFlags flags);
    VkRenderPassBeginInfo                  RenderPassBeginInfo(VkRenderPass pass, VkFramebuffer framebuffer, VkClearValue *clearVal, VkExtent2D extent);
    VkSubmitInfo                           SubmitInfo(FrameData *frame, ImageData *image, VkPipelineStageFlags *waitStage);
    VkPresentInfoKHR                       PresentInfo(ImageData *image, VkSwapchainKHR *swapchain, uint32_t *imageIndex);
    VkPipelineShaderStageCreateInfo        ShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule module);
    VkPipelineVertexInputStateCreateInfo   VertexInputStateCreateInfo();
    VkPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo(VkPrimitiveTopology topology);
    VkPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo(VkPolygonMode polygonMode);
    VkPipelineMultisampleStateCreateInfo   MultisampleStateCreateInfo();
    VkPipelineColorBlendAttachmentState    ColorBlendAttachmentState();
    VkPipelineLayoutCreateInfo             LayoutCreateInfo();
}
