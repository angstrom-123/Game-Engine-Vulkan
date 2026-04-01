#pragma once

#include "Util/allocator.h"

static constexpr size_t MAX_DESCRIPTORS = 10;
static constexpr size_t FRAMES_IN_FLIGHT = 3;
static constexpr size_t MAX_MATERIALS = 256;
static constexpr size_t MAX_UNIFORM_BUFFER_SIZE = 32 * 1024; // 32 KiB
    

struct FrameData {
    VkFence renderFence;
    VkSemaphore acquireSemaphore;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    AllocatedBuffer uniformBuffer;
    VkDescriptorPool descriptorPool;
    size_t descriptorOffset;
};

struct SwapchainImageData {
    VkFence flightFence;
    VkSemaphore renderSemaphore;
    VkImage image;
    VkImageView view;
    VkFramebuffer framebuffer;
    AllocatedImage depthImage;
    VkImageView depthView;
};

