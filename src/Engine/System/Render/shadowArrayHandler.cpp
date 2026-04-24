#include "shadowArrayHandler.h"
#include "renderTypes.h"
#include "vulkanBackend.h"

void ShadowArrayHandler::Init(VulkanBackend *backend)
{
    for (WritableTextureArray& array : shadowArrays) {
        array.Init(backend, backend->shadowRenderPass, SHADOWMAP_RESOLUTION, MAX_SHADOWCASTERS, VK_FORMAT_D32_SFLOAT, true);
    };
}

void ShadowArrayHandler::Cleanup(VkDevice device, VmaAllocator allocator)
{
    for (WritableTextureArray& array : shadowArrays) {
        array.Cleanup(device, allocator);
    }
}

ShadowAllocation ShadowArrayHandler::AllocateTexture(VulkanBackend *backend)
{
    uint32_t index = UINT32_MAX;
    for (WritableTextureArray& array : shadowArrays) {
        uint32_t arrayIndex = array.Allocate();
        ASSERT(index == arrayIndex || index == UINT32_MAX && "Shadow array desync");
        index = arrayIndex;
    }

    return (ShadowAllocation) {
        .textureID = index
    };
}
