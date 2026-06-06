#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "ECS/ecsTypes.h"
#include "System/Render/texture.h"
#include "attachmentArray.h"
#include "Util/enumIndex.h"

const uint32_t FRAMES_IN_FLIGHT = 2;
const uint32_t MAX_LIGHTS = 1024;
const uint32_t MAX_BLOOM_MIPS = 5;
const uint32_t MAX_NORMAL_MIPS = 16;
const uint32_t BLOOM_BLUR_RADIUS = 4;
const uint32_t SHADOW_CASCADE_COUNT = 3;

enum class TextureArrayID {
    COLOR_SMALL,
    COLOR_LARGE,
    DATA_SMALL,
    DATA_LARGE,
    FONT,
    MAX_ENUM
};

struct VulkanBackendSettings {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; 
    VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM; 
    VkFormat normalFormat = VK_FORMAT_R8G8B8A8_UNORM; 
    VkFormat materialFormat = VK_FORMAT_R8G8B8A8_UNORM; 
    VkFormat lightingFormat = VK_FORMAT_R16G16B16A16_SFLOAT; 
    VkFormat bloomFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat tonemapFormat = VK_FORMAT_R8G8B8A8_SRGB;
    uint32_t shadowResolution = 2048;
    VkFormat colorTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormat dataTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat fontTextureFormat = VK_FORMAT_R8_UNORM;
    uint32_t textureArrayResolutions[EnumBase(TextureArrayID::MAX_ENUM)] = {
        1024, // Color small
        2048, // Color large
        1024, // Data small
        2048, // Data large
        2048 // Font
    };
    uint32_t maxShadowcasters = 24;
    uint32_t maxLightsPerTile = 256;
    uint32_t computeTileSize = 16;

    bool vsync = true;
    bool bloomEnabled = true;
};

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
        uint32_t tileSize{0};
        uint32_t tilesX{0};
        uint32_t tilesY{0};
        uint32_t maxLightsPerTile{0};
    } lightCulling;
};

struct ToneMapPushConstants {
    uint32_t bloomEnabled{0};
};

struct NormalMipmapPushConstants {
    uint32_t srcIndex{0};
    uint32_t srcWidth{0};
    uint32_t srcHeight{0};
    uint32_t dstIndex{0};
    uint32_t dstWidth{0};
    uint32_t dstHeight{0};
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
        uint32_t diffuseIndex{0};
        uint32_t diffuseLayer{0};
        uint32_t normalIndex{0};
        uint32_t normalLayer{0};
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
    AllocatedBuffer shadowBuffer;           // Stores the shadowcasters in a flat array

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

struct AllocatedTexture {
    TextureArrayID arrayID{TextureArrayID::MAX_ENUM};
    uint32_t layerID{UINT32_MAX};
};

struct GraphicsFrontendInfo {
    Entity camera{INVALID_HANDLE};
    uint32_t maxShadows{0};
    uint32_t arrayLayers[EnumBase(TextureArrayID::MAX_ENUM)];
};

struct GraphicsFrontend {
    Entity camera{INVALID_HANDLE};
    uint32_t maxShadows{0};
    Texture arrayTextures[EnumBase(TextureArrayID::MAX_ENUM)];
    Texture shadowArrayTexture;
    // AttachmentArray shadowArray;
};

