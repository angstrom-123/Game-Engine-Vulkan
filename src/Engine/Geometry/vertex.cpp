#include "vertex.h"

VertexInputDesc Vertex::GetDepthVertexDesc()
{
    VertexInputDesc res;

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    res.bindings.push_back(binding);

    VkVertexInputAttributeDescription positionAttr = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, position)
    };

    res.attributes.push_back(positionAttr);

    return res;
}

VertexInputDesc Vertex::GetVertexDesc()
{
    VertexInputDesc res;

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    res.bindings.push_back(binding);

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
