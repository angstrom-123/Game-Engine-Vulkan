#pragma once

#include "Util/enumIndex.h"
#include "Util/stackVector.h"
#include "Geometry/vertex.h"

#include <deque>
#include <functional>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

const uint32_t MAX_PIPELINE_COLOR_ATTACHMENTS = 4;
const fs::path SHADER_PATH = "src/Engine/Resource/Shader";

enum class PipelineKind : uint32_t {
    NONE,
    GRAPHICS,
    COMPUTE
};

enum class ShaderKind : uint32_t {
    NONE,
    VERTEX,
    FRAGMENT,
    COMPUTE
};

enum class PipelineDepthFlag : uint32_t {
    NONE  = 0x0,
    WRITE = 0x1,
    TEST  = 0x2,
    BIAS  = 0x4,
};
using PipelineDepthFlags = uint32_t;

enum class PipelineInfoFlag : uint32_t {
    NONE              = 0x0,
    FRAGMENT_SHADER   = 0x1,
    VERTEX_SHADER     = 0x2,
    COMPUTE_SHADER    = 0x4,
    COLOR_ATTACHMENTS = 0x8,
    DEPTH_ATTACHMENT  = 0x10,
    BOUNDS            = 0x20,
    PUSH_CONSTANTS    = 0x40,
};
using PipelineInfoFlags = uint32_t;

// TODO: Push constants here are copied into shader code?
//       - But then I need to compile shaders at runtime
//       - Maybe ok since only once at backend startup?

struct DescriptorSets {
    VkDescriptorSet *sets;
    uint32_t count;
};

class Pipeline {
public:
    ~Pipeline() = default;
    Pipeline() = default;
    Pipeline& SetDevice(VkDevice device);
    Pipeline& SetKind(PipelineKind kind);
    Pipeline& SetName(const std::string& name);
    Pipeline& AddDescriptorLayout(VkDescriptorSetLayout layout);
    template<typename T> Pipeline& AddPushConstant(VkShaderStageFlags stage)
    {
        ASSERT(m_PushConstantsSize + sizeof(T) <= 128 && "Push constants too large");
        m_PushConstantRanges.EmplaceBack(stage, m_PushConstantsSize, sizeof(T));
        m_PushConstantsSize += sizeof(T);
        m_InfoFlags |= EnumBase(PipelineInfoFlag::PUSH_CONSTANTS);
        return *this;
    };
    Pipeline& AddShader(ShaderKind shaderKind, const fs::path& path);
    Pipeline& SetDynamicDepthAttachment(VkFormat format);
    Pipeline& SetDynamicColorAttachment(VkFormat format);
    Pipeline& AddColorAttachment(const class Texture& texture, 
            VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
    Pipeline& SetDepthWriteAttachment(const class Texture& texture, 
            VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
    Pipeline& SetDepthReadAttachment(const class Texture& texture, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
    Pipeline& SetBounds(VkViewport viewport, VkRect2D scissor, VkExtent2D extent);
    Pipeline& SetCulling(VkCullModeFlags culling);
    Pipeline& EnableBlending(bool enabled);
    Pipeline& SetDepthFlags(PipelineDepthFlags depthFlags); 
    Pipeline& SetDepthCompare(VkCompareOp compareOp);
    Pipeline& SetVertexInput(const VertexInputDesc& input);
    void Build();
    void Cleanup();
    void EnqueueCleanup(std::deque<std::function<void ()>>& deletionQueue);
    void PrepareForDynamicRendering(VkCommandBuffer commandBuffer, DescriptorSets descriptorSets);
    void BeginDynamicDepthRendering(VkCommandBuffer commandBuffer, 
            PFN_vkCmdBeginRenderingKHR beginCommand, VkImageView view, 
            VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
    void BeginDynamicColorRendering(VkCommandBuffer commandBuffer, 
            PFN_vkCmdBeginRenderingKHR beginCommand, VkImageView view, 
            VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp);
    void EndDynamicRendering(VkCommandBuffer commandBuffer, PFN_vkCmdEndRenderingKHR endCommand);
    void BeginRendering(VkCommandBuffer commandBuffer, 
            PFN_vkCmdBeginRenderingKHR beginCommand, DescriptorSets descriptorSets);
    void EndRendering(VkCommandBuffer commandBuffer, PFN_vkCmdEndRenderingKHR endCommand);
    void BeginCompute(VkCommandBuffer commandBufffer, DescriptorSets descriptorSets);
    void EndCompute();

    void UpdateDepthAttachment(const class Texture& texture);
    void UpdateColorAttachment(uint32_t index, const class Texture& texture);
    void UpdateBounds(VkViewport viewport, VkRect2D scissor, VkExtent2D extent);

    VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

private:
    void BuildGraphics();
    void BuildCompute();
    VkPipelineBindPoint BindPoint();

private:
    // State
    bool m_IsBuilt{false};
    bool m_IsComputing{false};
    bool m_IsRendering{false};
    VkDevice m_Device{VK_NULL_HANDLE};
    PipelineKind m_Kind{PipelineKind::NONE};
    std::string m_Name{"Pipeline"};
    PipelineInfoFlags m_InfoFlags{EnumBase(PipelineInfoFlag::NONE)};
    uint32_t m_PushConstantsSize{0};

    // Rendering
    bool m_DynamicDepthAttachment{false};
    bool m_DynamicColorAttachment{false};
    StackVector<VkRenderingAttachmentInfoKHR, MAX_PIPELINE_COLOR_ATTACHMENTS> m_ColorAttachments{};
    StackVector<VkFormat, MAX_PIPELINE_COLOR_ATTACHMENTS> m_ColorAttachmentFormats{};
    VkRenderingAttachmentInfoKHR m_DepthAttachment{};
    VkFormat m_DepthAttachmentFormat{VK_FORMAT_UNDEFINED};

    // Layout
    VkPipelineLayout m_PipelineLayout{VK_NULL_HANDLE};
    StackVector<VkPushConstantRange, 4> m_PushConstantRanges{};
    StackVector<VkDescriptorSetLayout, 8> m_DescriptorSetLayouts{};

    // Pipeline
    VkPipeline m_Pipeline{VK_NULL_HANDLE};
    VkPipelineVertexInputStateCreateInfo m_VertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr
    };
    VkCullModeFlags m_CullMode{VK_CULL_MODE_NONE};
    VkViewport m_Viewport{0};
    VkRect2D m_Scissor{0};
    VkExtent2D m_Extent{0};
    bool m_Blending{false};
    bool m_DepthWrite{false};
    bool m_DepthTest{false};
    bool m_DepthBias{false};
    VkCompareOp m_DepthCompare{VK_COMPARE_OP_ALWAYS};
    StackVector<VkShaderModule, 2> m_Shaders{}; 
};
