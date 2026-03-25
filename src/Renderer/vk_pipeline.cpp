#include <iostream>

#include "vk_pipeline.h"

VkPipeline PipelineBuilder::BuildPipeline(VkDevice device, VkRenderPass pass, VkViewport viewport, VkRect2D scissor)
{
    // No scissor stuff yet
    VkPipelineViewportStateCreateInfo viewportInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    // No color blending yet
    // This has to match the frag shader outputs
    VkPipelineColorBlendStateCreateInfo blendInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &m_ColorBlendAttachment,
    };

    const VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    const uint32_t nStages = m_ShaderStages.size();
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .stageCount = nStages,
        .pStages = m_ShaderStages.data(),
        .pVertexInputState = &m_VertexInputInfo,
        .pInputAssemblyState = &m_InputAssemblyInfo,
        .pViewportState = &viewportInfo,
        .pRasterizationState = &m_RasterizerInfo,
        .pMultisampleState = &m_MultisamplingInfo,
        .pColorBlendState = &blendInfo,
        .pDynamicState = &dynamicInfo,
        .layout = m_PipelineLayout,
        .renderPass = pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE
    };

    // Errors here are common, need better handling than just VK_CHECK
    VkPipeline res;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &res)) {
        std::cerr << "Error: " << __func__ << " : Failed to create graphics pipeline" << std::endl;
        return VK_NULL_HANDLE;
    } else {
        return res;
    }
}
