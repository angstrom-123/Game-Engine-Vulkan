#pragma once

#include "System/Render/renderTypes.h"
#include "Util/allocator.h"
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

namespace fs = std::filesystem;

struct MaterialInfo {
    fs::path vertexShader;
    fs::path fragmentShader;
    AllocatedImage textureImage;
    bool hasTransparency;
};

struct PushConstants {
    glm::mat4x4 mvp;
    glm::mat4x4 model;
};

struct VertexUniforms {
    glm::vec4 placeholder;
};

struct FragmentUniforms {
    glm::vec4 lightPos;
    glm::vec4 viewPos;
};

struct Material {
    bool hasTransparency;
    AllocatedImage textureImage;
    VkImageView textureView;
    VkSampler textureSampler;
    VkDescriptorSet descriptorSets[FRAMES_IN_FLIGHT];
};
