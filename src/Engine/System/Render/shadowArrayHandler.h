#pragma once

#include "renderTypes.h"
#include "writableTextureArray.h"

struct ShadowAllocation {
    uint32_t textureID;
};

class ShadowArrayHandler {
public:
    void Init(class VulkanBackend *backend);
    void Cleanup(VkDevice device, VmaAllocator allocator);
    ShadowAllocation AllocateTexture(class VulkanBackend *backend);

public:
    WritableTextureArray shadowArrays[FRAMES_IN_FLIGHT];
};
