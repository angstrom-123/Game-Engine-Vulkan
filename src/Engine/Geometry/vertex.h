#pragma once

#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>
#include <vector>

struct VertexInputDesc {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateFlags flags = 0;
};

// TODO: Packing vertex struct (snorm, etc)
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent; // Calculate bitangent in vertex shader

    static VertexInputDesc GetDepthVertexDesc();
    static VertexInputDesc GetVertexDesc();
};

