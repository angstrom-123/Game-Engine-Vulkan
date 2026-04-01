#pragma once

#include "System/Render/renderTypes.h"
#include "Util/allocator.h"
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

namespace fs = std::filesystem;

struct MaterialInfo {
    // TODO: Uniforms
    fs::path vertexShader;
    fs::path fragmentShader;
    AllocatedImage textureImage;
    size_t vertexUniformSize;
    size_t fragmentUniformSize;
};

struct MaterialPushConstants {
    glm::mat4x4 mvp;
};

struct Material {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet descriptorSets[FRAMES_IN_FLIGHT];
    VkDeviceSize vertexUniformOffset;
    VkDeviceSize fragmentUniformOffset;
    VkDeviceSize vertexUniformSize;
    VkDeviceSize fragmentUniformSize;
    VkPushConstantRange defaultConstantRange;
    MaterialPushConstants defaultConstants;
    AllocatedImage textureImage;
    VkImageView textureView;
    VkSampler textureSampler;
};
