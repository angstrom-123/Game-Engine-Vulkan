#include "textureArrayHandler.h"
#include "System/Render/renderTypes.h"
#include "Util/flags.h"
#include "Util/myAssert.h"
#include "vulkan_core.h"
#include "vulkanBackend.h"

void TextureArrayHandler::Init(const TextureArraySizes& sizes, VulkanBackend *backend)
{
    m_Arrays[TEXTURE_ARRAY_COLOR_1024].Init(1024, sizes.color1024, VK_FORMAT_R8G8B8A8_SRGB, backend);
    m_Arrays[TEXTURE_ARRAY_COLOR_2048].Init(2048, sizes.color2048, VK_FORMAT_R8G8B8A8_SRGB, backend);
    m_Arrays[TEXTURE_ARRAY_DATA_1024].Init(1024, sizes.data1024, VK_FORMAT_R8G8B8A8_UNORM, backend);
    m_Arrays[TEXTURE_ARRAY_DATA_2048].Init(2048, sizes.data2048, VK_FORMAT_R8G8B8A8_UNORM, backend);

    // Write descriptor sets for all the arrays at once
    VkDescriptorImageInfo imageInfos[TEXTURE_ARRAY_MAX_ENUM];
    for (uint32_t i = 0; i < TEXTURE_ARRAY_MAX_ENUM; i++) {
        imageInfos[i] = {
            .sampler = m_Arrays[i].GetSampler(),
            .imageView = m_Arrays[i].GetView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        FrameData& frame = backend->frames[i];
        for (uint32_t j = 0; j < TEXTURE_ARRAY_MAX_ENUM; j++) {
            VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = frame.textureDescriptorSet,
                .dstBinding = 0,
                .dstArrayElement = j,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[j]
            };
            vkUpdateDescriptorSets(backend->device, 1, &write, 0, nullptr);
        }
    }
}

void TextureArrayHandler::Cleanup(VkDevice device, VmaAllocator allocator)
{
    for (TextureArray& array : m_Arrays) {
        array.Cleanup(device, allocator);
    }
}

TextureAllocation TextureArrayHandler::AllocateTexture(ImageResource& imageData, VulkanBackend *backend)
{
    TextureArrayID arrayID;

    ASSERT(imageData.size.x <= 2048 && imageData.size.y <= 2048 && "Image too large to allocate in array");
    if (imageData.size.x <= 1024 && imageData.size.y <= 1024) {
        arrayID = FLAGS_HAVE_BIT(imageData.flags, IMAGE_FLAG_NON_COLOR) ? TEXTURE_ARRAY_DATA_1024 : TEXTURE_ARRAY_COLOR_1024;
    } else {
        arrayID = FLAGS_HAVE_BIT(imageData.flags, IMAGE_FLAG_NON_COLOR) ? TEXTURE_ARRAY_DATA_2048 : TEXTURE_ARRAY_COLOR_2048;
    }

    TextureArray& array = m_Arrays[arrayID];

    uint32_t layerIndex = array.Allocate(imageData, backend);

    return (TextureAllocation) {
        .arrayID = arrayID,
        .textureID = layerIndex,
    };
}
