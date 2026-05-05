#include "textureArrayHandler.h"
#include "ResourceManager/fontResource.h"
#include "ResourceManager/imageResource.h"
#include "System/Render/renderTypes.h"
#include "Util/flags.h"
#include "vulkan_core.h"
#include "vulkanBackend.h"

void TextureArrayHandler::Init(const TextureArraySizes& sizes, VulkanBackend *backend)
{
    m_Arrays[TEXTURE_ARRAY_COLOR_1024].Init(1024, sizes.color1024, VK_FORMAT_R8G8B8A8_SRGB, backend);
    m_Arrays[TEXTURE_ARRAY_COLOR_2048].Init(2048, sizes.color2048, VK_FORMAT_R8G8B8A8_SRGB, backend);
    m_Arrays[TEXTURE_ARRAY_DATA_1024].Init(1024, sizes.data1024, VK_FORMAT_R8G8B8A8_UNORM, backend);
    m_Arrays[TEXTURE_ARRAY_DATA_2048].Init(2048, sizes.data2048, VK_FORMAT_R8G8B8A8_UNORM, backend);
    m_Arrays[TEXTURE_ARRAY_FONT].Init(FONT_ATLAS_RESOLUTION, sizes.font, VK_FORMAT_R8_UNORM, backend);

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
    TextureArrayID arrayID = SelectArray(imageData.size, imageData.flags);
    TextureArray& array = m_Arrays[arrayID];
    uint32_t layerIndex = array.Allocate(imageData, backend);
    return (TextureAllocation) {
        .arrayID = arrayID,
        .layerID = layerIndex,
    };
}

TextureAllocation TextureArrayHandler::AllocateRaw(uint8_t *pixels, glm::ivec2 size, uint32_t flags, int32_t channels, VulkanBackend *backend)
{
    TextureArrayID arrayID = SelectArray(size, flags);
    TextureArray& array = m_Arrays[arrayID];
    ImageResource tmp(flags, size, pixels, channels);
    uint32_t layerIndex = array.Allocate(tmp, backend);
    return (TextureAllocation) {
        .arrayID = arrayID,
        .layerID = layerIndex,
    };
}

TextureArrayID TextureArrayHandler::SelectArray(glm::ivec2 size, uint32_t flags)
{
    if (FLAGS_HAVE_BIT(flags, IMAGE_FLAG_FONT_ATLAS)) {
        if (size.x == FONT_ATLAS_RESOLUTION && size.y == FONT_ATLAS_RESOLUTION) {
            return TEXTURE_ARRAY_FONT;
        }
        FATAL("Font atlas has wrong resolution: " << size.x << "x" << size.y << ". Expected: " << FONT_ATLAS_RESOLUTION << "x" << FONT_ATLAS_RESOLUTION);
    } else if (FLAGS_HAVE_BIT(flags, IMAGE_FLAG_NON_COLOR)) {
        if (size.x <= 1024 && size.y <= 1024) {
            return TEXTURE_ARRAY_DATA_1024;
        } else if (size.x <= 2048 && size.y <= 2048) {
            return TEXTURE_ARRAY_DATA_2048;
        }
        FATAL("Texture too large to allocate into data array");
    } else if (FLAGS_HAVE_BIT(flags, IMAGE_FLAG_TRANSPARENT) || flags == 0) {
        if (size.x <= 1024 && size.y <= 1024) {
            return TEXTURE_ARRAY_COLOR_1024;
        } else if (size.x <= 2048 && size.y <= 2048) {
            return TEXTURE_ARRAY_COLOR_2048;
        } 
        FATAL("Texture too large to allocate into color array");
    }
    FATAL("Failed to allocate texture in arrays");
}
