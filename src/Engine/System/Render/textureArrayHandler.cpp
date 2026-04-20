#include "textureArrayHandler.h"
#include "System/Render/renderTypes.h"
#include "Util/imageLoader.h"
#include "Util/myAssert.h"
#include "vulkan_core.h"

void TextureArrayHandler::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, CommandSubmitter& submitter, VmaAllocator allocator, FrameData *frames, TextureArraySizes& sizes)
{
    m_Arrays[TEXTURE_ARRAY_COLOR_1024].Init(device, graphicsQueue, submitter, allocator, 1024, sizes.color1024, VK_FORMAT_R8G8B8A8_SRGB);
    m_Arrays[TEXTURE_ARRAY_COLOR_2048].Init(device, graphicsQueue, submitter, allocator, 2048, sizes.color2048, VK_FORMAT_R8G8B8A8_SRGB);
    m_Arrays[TEXTURE_ARRAY_DATA_1024].Init(device, graphicsQueue, submitter, allocator, 1024, sizes.data1024, VK_FORMAT_R8G8B8A8_UNORM);
    m_Arrays[TEXTURE_ARRAY_DATA_2048].Init(device, graphicsQueue, submitter, allocator, 2048, sizes.data2048, VK_FORMAT_R8G8B8A8_UNORM);

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
        FrameData& frame = frames[i];
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
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    ImageData whiteImage, normalImage;
    whiteImage.LoadImage("src/Engine/Resource/Texture/white.png", false, IMAGE_KIND_COLOR);
    normalImage.LoadImage("src/Engine/Resource/Texture/normal.png", false, IMAGE_KIND_NORMAL);
    m_DefaultColorAllocation = AllocateTexture(device, physicalDevice, graphicsQueue, submitter, allocator, &whiteImage);
    m_DefaultNormalAllocation = AllocateTexture(device, physicalDevice, graphicsQueue, submitter, allocator, &normalImage);
}

void TextureArrayHandler::Cleanup(VkDevice device, VmaAllocator allocator)
{
    for (TextureArray& array : m_Arrays) {
        array.Cleanup(device, allocator);
    }
}

TextureAllocation TextureArrayHandler::AllocateTexture(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue, CommandSubmitter& submitter, VmaAllocator allocator, ImageData *imageData)
{
    if (m_AllocatedTextures.find(imageData->path) != m_AllocatedTextures.end()) {
        free(imageData->pixels);
        return m_AllocatedTextures[imageData->path];
    }

    TextureArrayID arrayID;
    ASSERT(imageData->width <= 2048 && imageData->height <= 2048 && "Image too large to allocate in array");
    if (imageData->width <= 1024 && imageData->height <= 1024) {
        arrayID = (imageData->kind == IMAGE_KIND_NORMAL) ? TEXTURE_ARRAY_DATA_1024 : TEXTURE_ARRAY_COLOR_1024;
    } else {
        arrayID = (imageData->kind == IMAGE_KIND_NORMAL) ? TEXTURE_ARRAY_DATA_2048 : TEXTURE_ARRAY_COLOR_2048;
    }

    TextureArray& array = m_Arrays[arrayID];

    uint32_t layerIndex = array.Allocate(device, physicalDevice, graphicsQueue, submitter, allocator, imageData);

    TextureAllocation res = {
        .arrayID = arrayID,
        .textureID = layerIndex,
    };

    m_AllocatedTextures.insert({std::string(imageData->path), res});

    return res;
}

TextureAllocation TextureArrayHandler::GetFallbackTexture(ImageData *imageData, ImageKind kind) 
{
    if (imageData) free(imageData->pixels);
    switch (kind) {
        case IMAGE_KIND_COLOR: return m_DefaultColorAllocation;
        case IMAGE_KIND_NORMAL: return m_DefaultNormalAllocation;
        default: VERIFY(false && "Unreachable");
    }
}
