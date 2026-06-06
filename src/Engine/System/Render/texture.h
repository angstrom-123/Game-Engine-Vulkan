#pragma once 

#include "ResourceManager/imageResource.h"
#include "Util/allocator.h"
#include "Util/stackQueue.h"
#include <deque>
#include <functional>
#include <glm/vec2.hpp>
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

class Texture {
public:
    ~Texture() = default;
    Texture() = default;
    Texture(VkDevice device, VmaAllocator allocator, 
            VkFormat format, VkImageUsageFlags usage, 
            glm::uvec2 size, VkImageAspectFlags aspect, 
            uint32_t layers, TextureFlags flags);
    Texture(VkDevice device, 
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
    void CopyToLayer(const ImageResource& image, class VulkanBackend& backend, uint32_t layer);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    void EnqueueCleanup(VkDevice device, VmaAllocator allocator, 
            std::deque<std::function<void ()>>& deletionQueue);

    VkImage GetImage() const { return m_Image.image; }
    VkFormat GetFormat() const { return m_Format; }
    VkImageView GetView() const;
    VkImageView GetLayerView(uint32_t layer) const;

private:
    void GenerateColorMips(const ImageResource& imageData, VulkanBackend& backend, uint32_t layer);
    void GenerateNormalMips(const ImageResource& imageData, VulkanBackend& backend, uint32_t layer);

private:
    TextureFlags m_Flags{0};
    bool m_Initialized{false};
    VkDevice m_Device{VK_NULL_HANDLE};
    AllocatedImage m_Image;
    VkImageView m_Views[MAX_ARRAY_LAYERS + 1]{VK_NULL_HANDLE}; // View 0 is the whole array view
    VkFormat m_Format{VK_FORMAT_UNDEFINED};
    VkImageLayout m_Layouts[MAX_ARRAY_LAYERS][MAX_MIP_LEVELS]{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageAspectFlags m_Aspect{VK_IMAGE_ASPECT_NONE};
    glm::uvec2 m_Size{0};
    uint32_t m_Layers{0};
    uint32_t m_Levels{0};
    StackQueue<uint32_t, MAX_ARRAY_LAYERS> m_FreeLayers;
};

static_assert(std::is_trivially_destructible_v<Texture>, 
        "Texture must have a trivial destructor to avoid memory leak in VulkanBackend");
