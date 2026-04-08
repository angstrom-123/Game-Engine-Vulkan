#pragma once

#include "Util/allocator.h"

static constexpr size_t FRAMES_IN_FLIGHT = 3;

struct FrameData {
    VkFence renderFence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet arrayDescriptorSet;
    VkDescriptorSet descriptorSet;
    AllocatedBuffer uniformBuffer;
};

struct SwapchainImageData {
    VkFence flightFence;
    VkSemaphore renderSemaphore;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
    AllocatedImage depthImage;
    VkImageView depthView;
    AllocatedImage msaaColorImage;
    VkImageView msaaColorView;
};
