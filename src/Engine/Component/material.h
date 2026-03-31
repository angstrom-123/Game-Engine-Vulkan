#pragma once

#include <filesystem>
#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_core.h>

namespace fs = std::filesystem;

struct MaterialInfo {
    // TODO: Uniforms
    fs::path vertexShader;
    fs::path fragmentShader;
};

struct MaterialPushConstants {
    glm::mat4x4 mvp;
};

struct Material {
    size_t index;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorLayout;
    VkPushConstantRange defaultConstantRange;
    MaterialPushConstants defaultConstants;
    // TODO: Uniforms
    // TODO: Textures (Image views?)
};
