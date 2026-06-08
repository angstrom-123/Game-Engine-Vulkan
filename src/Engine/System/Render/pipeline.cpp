#include "pipeline.h"
#include "backendDefs.h"
#include "Util/macros.h"
#include <vulkan/vulkan_core.h>
#include "texture.h"

#include <fstream>

#define ENABLE_PIPELINE_LOGGING 0

#if ENABLE_PIPELINE_LOGGING
    #define PIPELINE_LOG(msg) INFO(msg)
#else 
    #define PIPELINE_LOG(msg)
#endif

Pipeline& Pipeline::SetDevice(VkDevice device)
{
    m_Device = device;
    return *this;
}

Pipeline& Pipeline::SetKind(PipelineKind kind)
{
    m_Kind = kind;
    return *this;
}

Pipeline& Pipeline::SetName(const std::string& name)
{
    m_Name = name;
    return *this;
}

Pipeline& Pipeline::SetDynamicDepthAttachment(VkFormat format)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have attachments");
    ASSERT(!m_DynamicColorAttachment && "Cannot have dynamic depth and color");
    ASSERT(!m_DynamicDepthAttachment && "Already have a dynamic depth attachment");
    m_DynamicDepthAttachment = true;
    m_DepthAttachmentFormat = format;
    return *this;
}

Pipeline& Pipeline::SetDynamicColorAttachment(VkFormat format)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have attachments");
    ASSERT(!m_DynamicDepthAttachment && "Cannot have dynamic depth and color");
    ASSERT(!m_DynamicColorAttachment && "Already have a dynamic color attachment");
    m_DynamicColorAttachment = true;
    m_ColorAttachments.EmplaceBack(); // Dummy emplace to increase the size so the count is correct
    m_ColorAttachmentFormats.PushBack(format);
    return *this;
}

Pipeline& Pipeline::AddDescriptorLayout(VkDescriptorSetLayout layout)
{
    ASSERT(!m_IsBuilt && "Already built");
    m_DescriptorSetLayouts.PushBack(layout);
    return *this;
}

Pipeline& Pipeline::AddShader(ShaderKind shaderKind, const fs::path& path)
{
    ASSERT(!m_IsBuilt && "Already built");
    fs::path fullPath = SHADER_PATH / path;
    switch (shaderKind) {
        case ShaderKind::VERTEX:
            ASSERT(m_Shaders.Empty() && "Vertex shader must be the first shader");
            ASSERT(m_Kind == PipelineKind::GRAPHICS && "Vertex shader must be in a graphics pipeline");
            fullPath.replace_extension(".vert.spirv");
            m_InfoFlags |= EnumBase(PipelineInfoFlag::VERTEX_SHADER);
            break;
        case ShaderKind::FRAGMENT:
            ASSERT(!m_Shaders.Empty() && "Fragment shader cannot be the first shader");
            ASSERT(m_Kind == PipelineKind::GRAPHICS && "Fragment shader must be in a graphics pipeline");
            fullPath.replace_extension(".frag.spirv");
            m_InfoFlags |= EnumBase(PipelineInfoFlag::FRAGMENT_SHADER);
            break;
        case ShaderKind::COMPUTE:
            ASSERT(m_Shaders.Empty() && "Compute shader must be the only shader");
            ASSERT(m_Kind == PipelineKind::COMPUTE && "Compute shader must be in a compute pipeline");
            fullPath.replace_extension(".comp.spirv");
            m_InfoFlags |= EnumBase(PipelineInfoFlag::COMPUTE_SHADER);
            break;
        default:
            FATAL("Bad shader kind"); 
    };
    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        FATAL("Failed to open shader file");
    }

    size_t fileSize = file.tellg();
    uint32_t *buf = new uint32_t[fileSize];

    file.seekg(0);
    file.read((char *) buf, fileSize);
    file.close();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fileSize,
        .pCode = buf,
    };
    VK_CHECK(vkCreateShaderModule(m_Device, &info, nullptr, &shaderModule));

    delete[] buf;

    m_Shaders.PushBack(shaderModule);

    return *this;
}

Pipeline& Pipeline::AddColorAttachment(const Texture& texture, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have attachments");
    ASSERT(!m_DynamicColorAttachment && "Cannot have both static and dynamic color attachments");
    m_ColorAttachments.EmplaceBack((VkRenderingAttachmentInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = texture.GetView(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = loadOp,
        .storeOp = storeOp
    });
    m_ColorAttachmentFormats.EmplaceBack(texture.GetFormat());
    m_InfoFlags |= EnumBase(PipelineInfoFlag::COLOR_ATTACHMENTS);
    return *this;
}

Pipeline& Pipeline::SetDepthWriteAttachment(const Texture& texture, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have attachments");
    ASSERT(!m_DynamicDepthAttachment && "Cannot have both static and dynamic depth attachments");
    m_DepthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = texture.GetView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = loadOp,
        .storeOp = storeOp,
        .clearValue = { .depthStencil = { 1.0, 0 } }
    };
    m_DepthAttachmentFormat = texture.GetFormat();
    m_InfoFlags |= EnumBase(PipelineInfoFlag::DEPTH_ATTACHMENT);
    return *this;
}

Pipeline& Pipeline::SetDepthReadAttachment(const Texture& texture, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have attachments");
    ASSERT(!m_DynamicDepthAttachment && "Cannot have both static and dynamic depth attachments");
    m_DepthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = texture.GetView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        .loadOp = loadOp,
        .storeOp = storeOp,
        .clearValue = { .depthStencil = { 1.0, 0 } }
    };
    m_DepthAttachmentFormat = texture.GetFormat();
    m_InfoFlags |= EnumBase(PipelineInfoFlag::DEPTH_ATTACHMENT);
    return *this;
}

Pipeline& Pipeline::SetBounds(VkViewport viewport, VkRect2D scissor, VkExtent2D extent)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have bounds");
    m_Viewport = viewport;
    m_Scissor = scissor;
    m_Extent = extent;
    m_InfoFlags |= EnumBase(PipelineInfoFlag::BOUNDS);
    return *this;
}

Pipeline& Pipeline::SetCulling(VkCullModeFlags culling)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have culling");
    m_CullMode = culling;
    return *this;
}

Pipeline& Pipeline::EnableBlending(bool enabled)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have blending");
    m_Blending = enabled;
    return *this;
}

Pipeline& Pipeline::SetDepthFlags(PipelineDepthFlags flags)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have depth flags");
    m_DepthWrite = flags & EnumBase(PipelineDepthFlag::WRITE);
    m_DepthTest = flags & EnumBase(PipelineDepthFlag::TEST);
    m_DepthBias = flags & EnumBase(PipelineDepthFlag::BIAS);
    return *this;
}

Pipeline& Pipeline::SetDepthCompare(VkCompareOp compareOp)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have depth compare");
    m_DepthCompare = compareOp;
    return *this;
}

Pipeline& Pipeline::SetVertexInput(const VertexInputDesc& input)
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines can have depth vertex input");
    m_VertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(input.bindings.Size()),
        .pVertexBindingDescriptions = input.bindings.Data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(input.attributes.Size()),
        .pVertexAttributeDescriptions = input.attributes.Data()
    };
    return *this;
}

void Pipeline::Build()
{
    ASSERT(!m_IsBuilt && "Already built");
    ASSERT(m_Device != VK_NULL_HANDLE && "Device not specified");

    // Create the pipeline layout
    VkPipelineLayoutCreateInfo pipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(m_DescriptorSetLayouts.Size()),
        .pSetLayouts = (m_DescriptorSetLayouts.Empty()) ? nullptr : m_DescriptorSetLayouts.Data(),
        .pushConstantRangeCount = static_cast<uint32_t>(m_PushConstantRanges.Size()),
        .pPushConstantRanges = (m_PushConstantRanges.Empty()) ? nullptr : m_PushConstantRanges.Data()
    };
    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineCreateInfo, nullptr, &m_PipelineLayout));

    // Create the pipeline
    switch (m_Kind) {
        case PipelineKind::GRAPHICS:
            ASSERT((m_InfoFlags & EnumBase(PipelineInfoFlag::BOUNDS)) || (m_InfoFlags & EnumBase(PipelineInfoFlag::VERTEX_SHADER))
                    && "Graphics pipeline must have bounds and a vertex shader");
            BuildGraphics();
            break;
        case PipelineKind::COMPUTE:
            ASSERT((m_InfoFlags & EnumBase(PipelineInfoFlag::COMPUTE_SHADER))
                    && "Compute pipeline must have a compute shader");
            BuildCompute();
            break;
        default:
            FATAL("Bad pipeline kind");
    };

    m_IsBuilt = true;
}

void Pipeline::Cleanup()
{
    ASSERT(m_IsBuilt && "Not built");
    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
}

void Pipeline::EnqueueCleanup(std::deque<std::function<void ()>>& deletionQueue) 
{
    ASSERT(m_IsBuilt && "Not built");
    deletionQueue.push_back([this] {
        Cleanup();
    });
}

void Pipeline::PrepareForDynamicRendering(VkCommandBuffer commandBuffer, DescriptorSets descriptorSets)
{
    PIPELINE_LOG("[" << m_Name << "] Prepare For Dynamic Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support dynamic rendering");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(m_DynamicDepthAttachment || m_DynamicColorAttachment && "No dynamic attachment specified");

    vkCmdBindPipeline(commandBuffer, BindPoint(), m_Pipeline);
    vkCmdBindDescriptorSets(commandBuffer, BindPoint(), m_PipelineLayout, 
            0, descriptorSets.count, descriptorSets.sets, 
            0, nullptr);
    vkCmdSetViewport(commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_Scissor);
}

void Pipeline::BeginDynamicDepthRendering(VkCommandBuffer commandBuffer, PFN_vkCmdBeginRenderingKHR beginCommand, VkImageView view, 
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
{
    PIPELINE_LOG("[" << m_Name << "] Begin Dynamic Depth Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support begin rendering");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(m_DynamicDepthAttachment && "No dynamic depth attachment specified");

    VkRenderingAttachmentInfoKHR attachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = loadOp,
        .storeOp = storeOp,
        .clearValue = { .depthStencil = { 1.0, 0 } }
    };

    VkRenderingInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = { .extent = m_Extent },
        .layerCount = 1,
        .pDepthAttachment = &attachmentInfo
    };
    beginCommand(commandBuffer, &renderingInfo);
    m_IsRendering = true;
}

void Pipeline::BeginDynamicColorRendering(VkCommandBuffer commandBuffer, PFN_vkCmdBeginRenderingKHR beginCommand, VkImageView view, 
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp)
{
    PIPELINE_LOG("[" << m_Name << "] Begin Dynamic Color Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support begin rendering");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(m_DynamicColorAttachment && "No dynamic color attachment specified");

    VkRenderingAttachmentInfoKHR attachmentInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = loadOp,
        .storeOp = storeOp
    };

    VkRenderingInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = { .extent = m_Extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = (m_InfoFlags & EnumBase(PipelineInfoFlag::DEPTH_ATTACHMENT))
                ? &m_DepthAttachment
                : nullptr
    };
    beginCommand(commandBuffer, &renderingInfo);
    m_IsRendering = true;
}

void Pipeline::EndDynamicRendering(VkCommandBuffer commandBuffer, PFN_vkCmdEndRenderingKHR endCommand)
{
    PIPELINE_LOG("[" << m_Name << "] End Dynamic Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support end rendering");
    ASSERT(m_IsRendering && "Rendering not begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(m_DynamicDepthAttachment || m_DynamicColorAttachment && "No dynamic attachment specified");
    endCommand(commandBuffer);
    m_IsRendering = false;
}

void Pipeline::BeginRendering(VkCommandBuffer commandBuffer, PFN_vkCmdBeginRenderingKHR beginCommand, DescriptorSets descriptorSets)
{
    PIPELINE_LOG("[" << m_Name << "] Begin Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support begin rendering");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(!m_DynamicDepthAttachment && !m_DynamicColorAttachment && "Dynamic attachment must not be specified");

    VkRenderingInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = { .extent = m_Extent },
        .layerCount = 1,
        .colorAttachmentCount = static_cast<uint32_t>(m_ColorAttachments.Size()),
        .pColorAttachments = (m_ColorAttachments.Empty())
                ? nullptr 
                : m_ColorAttachments.Data(),
        .pDepthAttachment = (m_InfoFlags & EnumBase(PipelineInfoFlag::DEPTH_ATTACHMENT))
                ? &m_DepthAttachment
                : nullptr
    };
    beginCommand(commandBuffer, &renderingInfo);
    m_IsRendering = true;

    vkCmdBindPipeline(commandBuffer, BindPoint(), m_Pipeline);
    vkCmdBindDescriptorSets(commandBuffer, BindPoint(), m_PipelineLayout, 
            0, descriptorSets.count, descriptorSets.sets, 
            0, nullptr);

    vkCmdSetViewport(commandBuffer, 0, 1, &m_Viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_Scissor);
}

void Pipeline::EndRendering(VkCommandBuffer commandBuffer, PFN_vkCmdEndRenderingKHR endCommand)
{
    PIPELINE_LOG("[" << m_Name << "] End Rendering");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Only graphics pipelines support end rendering");
    ASSERT(!m_IsComputing && "Computing already begun");
    ASSERT(m_IsRendering && "Rendering not begun");
    ASSERT(!m_DynamicDepthAttachment && !m_DynamicColorAttachment && "Dynamic attachment must not be specified");
    endCommand(commandBuffer);
    m_IsRendering = false;
}

void Pipeline::BeginCompute(VkCommandBuffer commandBuffer, DescriptorSets descriptorSets)
{
    PIPELINE_LOG("[" << m_Name << "] Begin Compute");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(!m_IsComputing && "Computing already begun");
    vkCmdBindPipeline(commandBuffer, BindPoint(), m_Pipeline);
    vkCmdBindDescriptorSets(commandBuffer, BindPoint(), m_PipelineLayout, 
            0, descriptorSets.count, descriptorSets.sets, 
            0, nullptr);
    m_IsComputing = true;
}

void Pipeline::EndCompute()
{
    PIPELINE_LOG("[" << m_Name << "] End Compute");
    ASSERT(m_IsBuilt && "Pipeline not built");
    ASSERT(!m_IsRendering && "Rendering already begun");
    ASSERT(m_IsComputing && "Computing not begun");
    m_IsComputing = false;
}

void Pipeline::UpdateDepthAttachment(const Texture& texture)
{
    ASSERT(!m_DynamicDepthAttachment && !m_DynamicColorAttachment && "Cannot update dynamic attachments");
    ASSERT((m_InfoFlags & EnumBase(PipelineInfoFlag::DEPTH_ATTACHMENT)) && "No depth attachment to update");
    m_DepthAttachment.imageView = texture.GetView();
}

void Pipeline::UpdateColorAttachment(uint32_t index, const Texture& texture)
{
    ASSERT(!m_DynamicDepthAttachment && !m_DynamicColorAttachment && "Cannot update dynamic attachments");
    ASSERT((m_InfoFlags & EnumBase(PipelineInfoFlag::COLOR_ATTACHMENTS)) && "No color attachments to update");
    ASSERT(index < m_ColorAttachments.Size() && "Updating color attachment out of bounds");
    m_ColorAttachments[index].imageView = texture.GetView();
}

void Pipeline::UpdateBounds(VkViewport viewport, VkRect2D scissor, VkExtent2D extent)
{
    ASSERT(m_Kind == PipelineKind::GRAPHICS && "Compute pipelines do not have bounds");
    m_Viewport = viewport;
    m_Scissor = scissor;
    m_Extent = extent;
}

void Pipeline::BuildGraphics()
{
    INFO("Creating graphics pipeline: " << m_Name);

    StackVector<VkPipelineShaderStageCreateInfo, 2> shaderStages = {};
    shaderStages[0] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = m_Shaders[0],
        .pName = "main"
    };
    shaderStages[1] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = m_Shaders[1],
        .pName = "main"
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo viewportState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &m_Viewport,
        .scissorCount = 1,
        .pScissors = &m_Scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizationState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode = m_CullMode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = (m_DepthBias) 
                ? VK_TRUE 
                : VK_FALSE,
        .depthBiasConstantFactor = (m_DepthBias) 
                ? 1.25f 
                : 0.0f,
        .depthBiasSlopeFactor = (m_DepthBias) 
                ? 1.75f 
                : 0.0f,
        .lineWidth = 1.0
    };

    VkPipelineMultisampleStateCreateInfo multisampleState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo stencilState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = (m_DepthTest) ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (m_DepthTest) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = m_DepthCompare,
        .minDepthBounds = 0.0,
        .maxDepthBounds = 1.0,
    };

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    StackVector<VkPipelineColorBlendAttachmentState, 8> blendAttachments = {};
    for (uint32_t i = 0; i < m_ColorAttachments.Size(); i++) {
        blendAttachments.EmplaceBack((VkPipelineColorBlendAttachmentState) {
            .blendEnable = (m_Blending) 
                    ? VK_TRUE 
                    : VK_FALSE,
            .srcColorBlendFactor = (m_Blending) 
                    ? VK_BLEND_FACTOR_SRC_ALPHA 
                    : VK_BLEND_FACTOR_ZERO,
            .dstColorBlendFactor = (m_Blending) 
                    ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA 
                    : VK_BLEND_FACTOR_ZERO,
            .srcAlphaBlendFactor = (m_Blending) 
                    ? VK_BLEND_FACTOR_ONE 
                    : VK_BLEND_FACTOR_ZERO,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | 
                    VK_COLOR_COMPONENT_B_BIT | 
                    VK_COLOR_COMPONENT_G_BIT | 
                    VK_COLOR_COMPONENT_A_BIT
        });
    };

    VkPipelineColorBlendStateCreateInfo blendState = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(blendAttachments.Size()),
        .pAttachments = (blendAttachments.Empty()) 
            ? nullptr 
            : blendAttachments.Data()
    };

    VkPipelineRenderingCreateInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = static_cast<uint32_t>(m_ColorAttachments.Size()),
        .pColorAttachmentFormats = (m_ColorAttachmentFormats.Empty()) 
                ? nullptr 
                : m_ColorAttachmentFormats.Data(),
        .depthAttachmentFormat = m_DepthAttachmentFormat
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = static_cast<uint32_t>(m_Shaders.Size()),
        .pStages = shaderStages.Data(),
        .pVertexInputState = &m_VertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &stencilState,
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamicState,
        .layout = m_PipelineLayout
    };

    VK_CHECK(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline));
    for (VkShaderModule shaderModule : m_Shaders) {
        vkDestroyShaderModule(m_Device, shaderModule, nullptr);
    }
}

void Pipeline::BuildCompute()
{
    INFO("Creating compute pipeline: " << m_Name);

    VkComputePipelineCreateInfo computePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = m_Shaders[0],
            .pName = "main"
        },
        .layout = m_PipelineLayout
    };

    VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &m_Pipeline));

    vkDestroyShaderModule(m_Device, m_Shaders[0], nullptr);
}

VkPipelineBindPoint Pipeline::BindPoint()
{
    switch (m_Kind) {
        case PipelineKind::GRAPHICS: 
            return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case PipelineKind::COMPUTE: 
            return VK_PIPELINE_BIND_POINT_COMPUTE;
        default: 
            FATAL("Bad pipeline kind");
    }
}
