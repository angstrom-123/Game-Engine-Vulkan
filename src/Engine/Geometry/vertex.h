#pragma once

#include "Util/stackVector.h"
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>

struct VertexInputDesc {
    StackVector<VkVertexInputBindingDescription, 8> bindings;
    StackVector<VkVertexInputAttributeDescription, 8> attributes;
};

// TODO: Packing vertex struct?
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent; // Calculate bitangent in vertex shader

    static VertexInputDesc GetDepthVertexDesc();
    static VertexInputDesc GetVertexDesc();
};

