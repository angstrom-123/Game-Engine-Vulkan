#include <vector>

#include "vulkan_core.h"

class PipelineBuilder {
public:
    std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStages;
    VkPipelineVertexInputStateCreateInfo m_VertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo m_InputAssemblyInfo;
    VkPipelineRasterizationStateCreateInfo m_RasterizerInfo;
    VkPipelineColorBlendAttachmentState m_ColorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_MultisamplingInfo;
    VkPipelineLayout m_PipelineLayout;

    VkPipeline BuildPipeline(VkDevice device, VkRenderPass pass, VkViewport viewport, VkRect2D scissor);
};

