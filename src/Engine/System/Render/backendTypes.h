#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "ECS/ecsTypes.h"
#include "System/Render/attachmentArray.h"
#include "textureArray.h"

const uint32_t FRAMES_IN_FLIGHT = 2;
const VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT; 
const VkFormat ALBEDO_FORMAT = VK_FORMAT_R8G8B8A8_UNORM; 
const VkFormat NORMAL_FORMAT = VK_FORMAT_A2R10G10B10_UNORM_PACK32; 
const VkFormat MATERIAL_FORMAT = VK_FORMAT_R8G8B8A8_UNORM; 
const VkFormat LIGHTING_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT; 
const VkFormat BLOOM_FORMAT = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
const VkFormat TONE_MAP_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;
const uint32_t SHADOW_RESOLUTION = 4096;
const uint32_t COLOR_SMALL_RESOLUTION = 1024;
const uint32_t COLOR_LARGE_RESOLUTION = 2048;
const uint32_t DATA_SMALL_RESOLUTION = 1024;
const uint32_t DATA_LARGE_RESOLUTION = 2048;
const uint32_t FONT_RESOLUTION = 2048;
const uint32_t MAX_LIGHTS = 1024;
const uint32_t MAX_LIGHTS_PER_TILE = 256;
const uint32_t COMPUTE_TILE_SIZE = 16;
const uint32_t MAX_BLOOM_MIPS = 16;
const uint32_t BLOOM_BLUR_RADIUS = 7;

struct PerFrameUniforms {
    struct {
        glm::mat4x4 view{0.0};
        glm::mat4x4 proj{0.0};
        glm::mat4x4 invView{0.0};
        glm::mat4x4 invProj{0.0};
        glm::vec4 position{0.0};
        float near{0.0};
        float far{0.0};
    } camera;

    struct {
        float exposure{0.0};
        float gamma{0.0};
        float ambientIntensity{0.0};
        uint32_t lightCount{0};
        glm::ivec2 screenSize{0};
        glm::ivec2 shadowSize{0};
    } settings;

    struct {
        uint32_t tileSize{COMPUTE_TILE_SIZE};
        uint32_t tilesX{0};
        uint32_t tilesY{0};
        uint32_t maxLightsPerTile{MAX_LIGHTS_PER_TILE};
    } lightCulling;
};

struct BloomPushConstants {
    uint32_t currentIndex{0};
    uint32_t blurKernelRadius{0};
    float blurKernelWeights[BLOOM_BLUR_RADIUS * 2 + 1];
    float accumulationFactor{0.0};
};

struct ShadowPushConstants {
    glm::mat4x4 model{0.0};
    glm::mat4x4 vp{0.0};
};

struct VertexPushConstants {
    glm::mat4x4 model{0.0};
};

// 64 byte offset, maximum size: 128-64 = 64 bytes
struct FragmentPushConstants {
    struct Material {
        glm::vec4 albedo{1.0}; // rgba
        float roughness{1.0};
        float metallic{0.0};
        float ao{0.0};
        float emissive{0.0};
        uint32_t diffuseTexture{0};
        uint32_t normalTexture{0};
        uint32_t flags{0};
    } material;
};

struct FrameData {
    VkFence renderFence{VK_NULL_HANDLE};
    VkSemaphore acquireSemaphore{VK_NULL_HANDLE};

    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};

    VkDescriptorSet dummyDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet0{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet1{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet2{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet3{VK_NULL_HANDLE};

    AllocatedBuffer uniformBuffer;
    AllocatedBuffer lightBuffer;            // Stores the lights in a flat array
    AllocatedBuffer lightIndexBuffer;       // Stores the indices into the light buffer / tile
    AllocatedBuffer lightTileCountBuffer;   // Stores the number of lights / tile

#ifdef PROFILING
    VkQueryPool queryPool;
    uint32_t queryCount;
    std::vector<uint64_t> queryTimestamps;
#endif
};

struct SwapchainImageData {
    VkFence flightFence{VK_NULL_HANDLE};
    VkSemaphore renderSemaphore{VK_NULL_HANDLE};
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct OffscreenTargetArray {
    uint32_t count{0};
    AllocatedImage images[MAX_BLOOM_MIPS];
    VkImageView views[MAX_BLOOM_MIPS]{VK_NULL_HANDLE};
    glm::ivec2 sizes[MAX_BLOOM_MIPS];
};

struct OffscreenTarget {
    AllocatedImage image;
    VkImageView view{VK_NULL_HANDLE};
};

enum TextureArrayID {
    TEXTURE_ARRAY_COLOR_SMALL,
    TEXTURE_ARRAY_COLOR_LARGE,
    TEXTURE_ARRAY_DATA_SMALL,
    TEXTURE_ARRAY_DATA_LARGE,
    TEXTURE_ARRAY_FONT,
    TEXTURE_ARRAY_MAX_ENUM
};

struct AllocatedTexture {
    TextureArrayID arrayID = TEXTURE_ARRAY_MAX_ENUM;
    uint32_t layerID = UINT32_MAX;
};

struct GraphicsFrontendInfo {
    Entity camera{INVALID_HANDLE};
    uint32_t maxShadows{0};
    uint32_t arrayLayers[TEXTURE_ARRAY_MAX_ENUM];
};

struct GraphicsFrontend {
    Entity camera{INVALID_HANDLE};

    TextureArray textureArrays[TEXTURE_ARRAY_MAX_ENUM] {
        { COLOR_SMALL_RESOLUTION, VK_FORMAT_R8G8B8A8_SRGB },    // Color small
        { COLOR_LARGE_RESOLUTION, VK_FORMAT_R8G8B8A8_SRGB },    // Color large
        { DATA_SMALL_RESOLUTION, VK_FORMAT_R8G8B8A8_UNORM },    // Data small
        { DATA_LARGE_RESOLUTION, VK_FORMAT_R8G8B8A8_UNORM },    // Data large
        { FONT_RESOLUTION, VK_FORMAT_R8_UNORM }                 // Font
    };

    bool shadowsEnabled{false};
    AttachmentArray shadowArray{ SHADOW_RESOLUTION, VK_FORMAT_D32_SFLOAT };
};

