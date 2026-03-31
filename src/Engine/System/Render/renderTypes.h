#pragma once

#include "Engine/Util/allocator.h"

static constexpr size_t MAX_DESCRIPTORS = 4;
static constexpr size_t MAX_MATERIALS = 32;
static constexpr size_t FRAMES_IN_FLIGHT = 3;

struct FrameData {
    VkFence renderFence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    AllocatedBuffer uniformBuffer;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSets[MAX_MATERIALS];
};

struct ImageData {
    VkFence flightFence;
    VkSemaphore renderSemaphore;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
    AllocatedImage depthImage;
    VkImageView depthView;
};

