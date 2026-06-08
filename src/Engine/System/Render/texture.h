#pragma once 

#include "ResourceManager/imageResource.h"
#include "commandSubmitter.h"
#include "mipGenerator.h"
#include "device.h"
#include "Util/allocator.h"
#include "Util/stackQueue.h"
#include <deque>
#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <type_traits>
#include <vulkan/vulkan_core.h>

const uint32_t MAX_ARRAY_LAYERS = 128;
const uint32_t MAX_MIP_LEVELS = 12;

enum class TextureFlag : uint32_t {
    NONE        = 0x0,
    NON_COLOR   = 0x1,
    LAYER_VIEWS = 0x2,
    MIP_MAPS    = 0x4,
    NO_VIEW     = 0x8,
};
using TextureFlags = uint32_t;

enum class TextureLayout : uint8_t {
    UNDEFINED,
    GENERAL,
    COLOR_ATTACHMENT,
    DEPTH_ATTACHMENT,
    DEPTH_STENCIL_READ_ONLY,
    SHADER_READ_ONLY,
    TRANSFER_SRC,
    TRANSFER_DST,
    PRESENT_SRC
};
constexpr VkImageLayout ToVulkanLayout(TextureLayout layout)
{
    switch (layout) {
        case TextureLayout::UNDEFINED: return VK_IMAGE_LAYOUT_UNDEFINED;
        case TextureLayout::GENERAL: return VK_IMAGE_LAYOUT_UNDEFINED;
        case TextureLayout::COLOR_ATTACHMENT: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case TextureLayout::DEPTH_ATTACHMENT: return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case TextureLayout::DEPTH_STENCIL_READ_ONLY: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case TextureLayout::SHADER_READ_ONLY: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case TextureLayout::TRANSFER_SRC: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case TextureLayout::TRANSFER_DST: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case TextureLayout::PRESENT_SRC: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:
            UNIMPLEMENTED("Other texture layout");
    };
}
constexpr TextureLayout FromVulkanLayout(VkImageLayout layout)
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return TextureLayout::UNDEFINED;
        case VK_IMAGE_LAYOUT_GENERAL: return TextureLayout::GENERAL;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return TextureLayout::COLOR_ATTACHMENT;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return TextureLayout::DEPTH_ATTACHMENT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return TextureLayout::DEPTH_STENCIL_READ_ONLY;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return TextureLayout::SHADER_READ_ONLY;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return TextureLayout::TRANSFER_SRC;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return TextureLayout::TRANSFER_DST;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return TextureLayout::PRESENT_SRC;
        default:
            UNIMPLEMENTED("Other texture layout");
    };
}

class Texture {
public:
    ~Texture() = default;
    Texture() = default;
    Texture(const Device& device, 
            VkFormat format, VkImageUsageFlags usage, 
            glm::uvec2 size, VkImageAspectFlags aspect, 
            uint32_t layers, TextureFlags flags);
    Texture(const Device& device, 
            VkImage image, VkImageView view, VkFormat format, 
            glm::uvec2 size, VkImageAspectFlags aspect);
    void Transition(VkCommandBuffer commandBuffer, 
            VkAccessFlags srcAccess, VkAccessFlags dstAccess, 
            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, 
            VkImageLayout newLayout);
    void TransitionLayer(VkCommandBuffer commandBuffer, 
            VkAccessFlags srcAccess, VkAccessFlags dstAccess, 
            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, 
            VkImageLayout newLayout, uint32_t layer);
    void TransitionExplicit(VkCommandBuffer commandBuffer,
            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
            uint32_t firstLayer, uint32_t layerCount,
            uint32_t firstMip, uint32_t mipCount,
            VkImageLayout newLayout);
    uint32_t AllocateLayer();
    void CopyToLayer(const Device& device, const CommandSubmitter& submitter, std::optional<MipGenerator>&& mipGenerator, const ImageResource& image, uint32_t layer);
    void Cleanup(const Device& device);
    void EnqueueCleanup(const Device& device, std::deque<std::function<void ()>>& deletionQueue);

    VkImage GetImage() const { return m_Image.image; }
    VkFormat GetFormat() const { return m_Format; }
    VkImageView GetView() const;
    VkImageView GetLayerView(uint32_t layer) const;

private:
    void GenerateCustomMips(const Device& device, const CommandSubmitter& submitter, MipGenerator& mipGenerator, const ImageResource& image, uint32_t layer);
    void GenerateDefaultMips(const Device& device, const CommandSubmitter& submitter, const ImageResource& image, uint32_t layer);

private:
    // TODO: Parameterize amount of layers and mips in template
    TextureFlags m_Flags{0};
    bool m_Initialized{false};
    AllocatedImage m_Image;
    VkImageView m_Views[MAX_ARRAY_LAYERS + 1]{VK_NULL_HANDLE}; // View 0 is the whole array view
    VkFormat m_Format{VK_FORMAT_UNDEFINED};
    TextureLayout m_Layouts[MAX_ARRAY_LAYERS][MAX_MIP_LEVELS]{TextureLayout::UNDEFINED};
    VkImageAspectFlags m_Aspect{VK_IMAGE_ASPECT_NONE};
    glm::uvec2 m_Size{0};
    uint32_t m_Layers{0};
    uint32_t m_Levels{0};
    StackQueue<uint32_t, MAX_ARRAY_LAYERS> m_FreeLayers;
};

static_assert(std::is_trivially_destructible_v<Texture>, 
        "Texture must have a trivial destructor to avoid memory leak in VulkanBackend");
