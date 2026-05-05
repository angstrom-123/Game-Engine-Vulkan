#pragma once

#include "System/Render/renderTypes.h"
#include <vulkan/vulkan_core.h>

#include <vector>

namespace vkinit {
    VkCommandBufferBeginInfo CommandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0);
    VkRenderPassBeginInfo RenderPassBeginInfo(VkRenderPass pass, VkFramebuffer framebuffer, VkClearValue *clearValues, uint32_t clearValueCount, VkExtent2D extent);
    VkFramebufferCreateInfo FramebufferCreateInfo(VkRenderPass pass, VkExtent2D extent);
    VkSubmitInfo SubmitInfo(FrameData *frame, SwapchainImageData *image, VkPipelineStageFlags *waitStage);
    VkSubmitInfo SubmitInfo(VkCommandBuffer *commandBuffer);
    VkPresentInfoKHR PresentInfo(SwapchainImageData *image, VkSwapchainKHR *swapchain, uint32_t *imageIndex);
    VkSemaphoreCreateInfo SemaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);
    VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags = 0);
    VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queueFamilyIndex);
    VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool);
    VkPipelineShaderStageCreateInfo ShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule module);
    VkPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo(std::vector<VkVertexInputBindingDescription> *bindings = nullptr, std::vector<VkVertexInputAttributeDescription> *attributes = nullptr);
    VkPipelineInputAssemblyStateCreateInfo InputAssemblyStateCreateInfo(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    VkPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo(VkPolygonMode polygonMode, VkCullModeFlags cullMode);
    VkPipelineMultisampleStateCreateInfo MultisampleStateCreateInfo(VkSampleCountFlagBits samples, bool enableA2C);
    VkPipelineColorBlendAttachmentState ColorBlendAttachmentState();
    VkPipelineLayoutCreateInfo LayoutCreateInfo(VkPushConstantRange *pushConstant = nullptr);
    VkDescriptorSetLayoutCreateInfo DescriptorSetLayoutCreateInfo(uint32_t binding, VkDescriptorType type, VkShaderStageFlags flags);
    VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usage, VkExtent3D extent);
    VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspect);
    VkPipelineDepthStencilStateCreateInfo DepthStencilStateCreateInfo(bool depthTest, bool depthWrite, VkCompareOp compare);
}
