#pragma once

#include "Util/allocator.h"
#include "glm/ext/matrix_float4x4.hpp"

static constexpr size_t FRAMES_IN_FLIGHT = 3;
static constexpr size_t MAX_LIGHTS = 1024;
static constexpr size_t MAX_TILE_LIGHTS = 256;
static constexpr size_t TILE_SIZE = 16; // Another common size is 32

struct LightCullingPushConstants {
    uint32_t tileSize;
    uint32_t tilesX;
    uint32_t tilesY;
    uint32_t lightCount;
    uint32_t maxTileLights;
    float near;
    float far;
    uint32_t screenWidth;
    uint32_t screenHeight;
};

struct DepthPushConstants {
    glm::mat4x4 model;
    glm::mat4x4 vp;
};

struct PushConstants {
    glm::mat4x4 model;
    float specularExponent;
    uint32_t ambientIndex;
    uint32_t diffuseIndex;
    uint32_t normalIndex;
    uint32_t tileSize;
    uint32_t tilesX;
    uint32_t tilesY;
    uint32_t maxTileLights;
};

struct GlobalUniforms {
    glm::mat4x4 vp;
};

struct CameraUniforms {
    glm::mat4x4 view;
    glm::mat4x4 projection;
    glm::vec3 viewPos;
};

struct LightCullingData {
    AllocatedBuffer lightBuffer;
    AllocatedBuffer lightIndices;
    AllocatedBuffer tileCounts;
    VkDescriptorSet descriptorSet;
    AllocatedBuffer uniformBuffer;
};

struct FrameData {
    VkFence renderFence;
    VkSemaphore acquireSemaphore;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkDescriptorPool descriptorPool;
    VkDescriptorSet arrayDescriptorSet;
    VkDescriptorSet resolveDescriptorSet;
    VkDescriptorSet descriptorSet;
    AllocatedBuffer uniformBuffer;
    LightCullingData lightCulling;
};

struct SwapchainImageData {
    VkFence flightFence;
    VkSemaphore renderSemaphore;

    VkFramebuffer framebuffer;
    VkFramebuffer depthFramebuffer;

    // Acquired from the swapchain, no need to allocate
    VkImage image;
    VkImageView view;

    AllocatedImage depthImage;
    VkImageView depthView;

    AllocatedImage resolvedDepthImage;
    VkImageView resolvedDepthView;

    AllocatedImage msaaColorImage;
    VkImageView msaaColorView;
};
