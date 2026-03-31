#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

class PipelineBuilder {
public:
    VkPipeline BuildPipeline(const VkDevice& device, const VkRenderPass& pass, const VkViewport& viewport, const VkRect2D& scissor);
    PipelineBuilder& SetVertexInput(const VkPipelineVertexInputStateCreateInfo& info);
    PipelineBuilder& SetInputAssembly(const VkPipelineInputAssemblyStateCreateInfo& info);
    PipelineBuilder& SetRasterizer(const VkPipelineRasterizationStateCreateInfo& info);
    PipelineBuilder& SetMultisampling(const VkPipelineMultisampleStateCreateInfo& info);
    PipelineBuilder& SetColorBlend(const VkPipelineColorBlendAttachmentState& state);
    PipelineBuilder& SetDepthStencil(const VkPipelineDepthStencilStateCreateInfo& info);
    PipelineBuilder& SetPipelineLayout(const VkPipelineLayout& layout);
    void MakeShader(const VkPipelineShaderStageCreateInfo& vertex, const VkPipelineShaderStageCreateInfo& fragment);

private:
    std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStages;
    VkPipelineVertexInputStateCreateInfo m_VertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo m_InputAssemblyInfo;
    VkPipelineRasterizationStateCreateInfo m_RasterizerInfo;
    VkPipelineColorBlendAttachmentState m_ColorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_MultisamplingInfo;
    VkPipelineDepthStencilStateCreateInfo m_DepthStencilInfo;
    VkPipelineLayout m_PipelineLayout;
};

