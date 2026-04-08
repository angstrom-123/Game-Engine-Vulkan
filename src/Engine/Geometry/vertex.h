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

    static VertexInputDesc GetVertexDesc()
    {
        VertexInputDesc res;

        VkVertexInputBindingDescription binding = {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        res.bindings.push_back(binding);

        // Decompose the Vertex struct into its components
        // These are the vertex shader inputs
        VkVertexInputAttributeDescription positionAttr = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position)
        };
        VkVertexInputAttributeDescription normalAttr = {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, normal)
        };
        VkVertexInputAttributeDescription uvAttr = {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, uv)
        };
        VkVertexInputAttributeDescription tangentAttr = {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(Vertex, tangent)
        };

        res.attributes.push_back(positionAttr);
        res.attributes.push_back(normalAttr);
        res.attributes.push_back(uvAttr);
        res.attributes.push_back(tangentAttr);

        return res;
    }
};

