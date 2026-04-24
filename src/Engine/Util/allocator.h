#pragma once

#include <VulkanMemoryAllocator/vk_mem_alloc.h>
#ifdef DEBUG
    #define VMA_NAME_ALLOCATION(allocator, allocation, name) vmaSetAllocationName(allocator, allocation, name);
#else 
    #define VMA_NAME_ALLOCATION(allocator, allocation, name)
#endif

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
