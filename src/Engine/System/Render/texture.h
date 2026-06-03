#pragma once 

#include "Util/allocator.h"
#include <deque>
#include <functional>
#include <glm/vec2.hpp>
#include <vulkan/vulkan_core.h>

// TODO: Separate class for array texture

class Texture {
public:
    Texture() = default;
    Texture(VkDevice device, VmaAllocator allocator, VkFormat format, VkImageUsageFlags usage, glm::uvec2 size, VkImageAspectFlags aspect);
    Texture(VkDevice device, VkImage image, VkImageView view, VkFormat format, glm::uvec2 size, VkImageAspectFlags aspect);
    void Transition(VkCommandBuffer commandBuffer, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkImageLayout newLayout);
    void EnqueueCleanup(VkDevice device, VmaAllocator allocator, std::deque<std::function<void ()>>& deletionQueue);

    VkImage GetImage() { return m_Image.image; }
    VkImageView GetView() { return m_View; }
    VkFormat GetFormat() { return m_Format; }

private:
    bool m_Allocated{false};
    VkDevice m_Device{VK_NULL_HANDLE};
    AllocatedImage m_Image;
    VkImageView m_View{VK_NULL_HANDLE};
    VkFormat m_Format{VK_FORMAT_UNDEFINED};
    VkImageLayout m_Layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageAspectFlags m_Aspect{VK_IMAGE_ASPECT_NONE};
    glm::uvec2 m_Size{0};
};
