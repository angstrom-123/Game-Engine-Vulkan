#pragma once

#include "System/Render/writableTextureArray.h"
#include "Util/allocator.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "Util/logger.h"
#include "vulkan_core.h"

#ifdef DEBUG 
    #define USE_VALIDATION_LAYERS VK_TRUE 
    #define VK_CHECK(x)\
        do {\
            VkResult __err = x;\
            if (__err) {\
                ERROR("Vulkan error: " << (VkResult) __err);\
                abort();\
            }\
        } while (0);
#elifdef RELEASE
    #define USE_VALIDATION_LAYERS VK_TRUE 
    // #define USE_VALIDATION_LAYERS VK_FALSE 
    #define VK_CHECK(x) (void) x
#else 
    #error "DEBUG or RELEASE must be specified"
#endif

static const size_t FRAMES_IN_FLIGHT = 2;
static const size_t MAX_LIGHTS = 512;
static const size_t MAX_SHADOWCASTERS = 8;
static const uint32_t SHADOWMAP_RESOLUTION = 2048;
static const size_t MAX_TILE_LIGHTS = 128;
static const size_t TILE_SIZE = 16;
static const size_t SSAO_SAMPLES = 16;

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
    uint32_t screenWidth;
    uint32_t screenHeight;
};

struct GlobalUniforms {
    glm::mat4x4 vp;
    glm::vec4 ssaoKernel[SSAO_SAMPLES];
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
    VkDescriptorSet textureDescriptorSet;
    VkDescriptorSet resolveDescriptorSet;
    VkDescriptorSet descriptorSet;
    AllocatedBuffer uniformBuffer;
    LightCullingData lightCulling;
    WritableTextureArray shadowArray;

#ifdef PROFILING
    VkQueryPool queryPool;
    uint32_t queryCount;
    std::vector<uint64_t> queryTimestamps;
#endif

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
