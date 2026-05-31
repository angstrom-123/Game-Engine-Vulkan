#include "vertex.h"

VertexInputDesc Vertex::GetDepthVertexDesc()
{
    VertexInputDesc res;

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    res.bindings.PushBack(binding);

    VkVertexInputAttributeDescription positionAttr = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, position)
    };
    VkVertexInputAttributeDescription uvAttr = {
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(Vertex, uv)
    };

    res.attributes.PushBack(positionAttr);
    res.attributes.PushBack(uvAttr);

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

    res.bindings.PushBack(binding);

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

    res.attributes.PushBack(positionAttr);
    res.attributes.PushBack(normalAttr);
    res.attributes.PushBack(uvAttr);
    res.attributes.PushBack(tangentAttr);

    return res;
}
