#pragma once

#include <VulkanMemoryAllocator/vk_mem_alloc.h>

struct AllocatedImage {
    VkImage image;
    VmaAllocation allocation;
    void *data = nullptr;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    void *data = nullptr;
};
