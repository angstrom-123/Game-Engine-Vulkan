#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "Component/material.h"
#include "Component/mesh.h"
#include "ECS/ecs.h"
#include "Util/allocator.h"
#include "config.h"
#include "renderTypes.h"

struct DeletionQueue {
    std::deque<std::function<void ()>> deletors;

    void PushFunction(std::function<void ()>&& function) { deletors.push_back(function); }
    void Clear() { deletors.clear(); }
    void Flush()
    {
        // Reverse iterator
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }
        deletors.clear();
    }
};

struct UploadContext {
    VkFence uploadFence;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    bool initialized;
};

struct TextureArray {
    AllocatedImage image;
    VkImageView view;
    VkFormat format;
    VkSampler sampler;
    uint32_t layerCount;
    uint32_t resolution;
    uint32_t mipLevels;
    std::queue<uint32_t> freeLayers;
};

class RenderSystem : public System {
public:
    void Init(struct GLFWwindow *window, Config *config);
    void SetCamera(Entity camera);
    void Cleanup();
    void Draw(ECS& ecs);
    void RequestResize();
    void AllocateMesh(Mesh& mesh);
    void AllocateMaterialTextures(const MaterialTextureInfo& info, Material& material);

    Signature GetSignature(ECS& ecs) { return ecs.GetBit<Transform>() | ecs.GetBit<Mesh>() | ecs.GetBit<Material>(); };
    size_t GetFrameNumber() { return m_FrameNum; };

private:
    void Resize(ECS& ecs);
    void InitVulkan(struct GLFWwindow *window);
    void InitMSAA(uint64_t requestedSamples);
    void InitSwapchain();
    void InitCommands();
    void InitDefaultRenderPass();
    void InitFramebuffers();
    void InitSyncStructures();
    void InitPipelines();
    void InitGlobalDescriptor();
    void ImmediateSubmit(std::function<void (VkCommandBuffer commandBuffer)>&& function);
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule& module);
    uint32_t CreateTextureArray(uint32_t resolution, uint32_t size, VkFormat format);
    uint32_t AllocateTexture(ImageData *imageData, TextureArray& array);
    size_t Align(size_t size);

private:
    // For Drawing
    Entity m_Camera;
    std::vector<Entity> m_DrawOrder;
    uint32_t m_DefaultColorTextureIndex;
    uint32_t m_DefaultNormalTextureIndex;

    // Texture Array
    // TODO: Add multiple sizes of array
    VkDescriptorSetLayout m_ArrayDescriptorLayout;
    uint32_t m_MaxTextureArrays;
    uint32_t m_ColorArrayIndex;
    uint32_t m_DataArrayIndex;
    std::vector<TextureArray> m_TextureArrays;

    // Global Descriptor
    VkDeviceSize m_UniformAlignment;
    VkDescriptorSetLayout m_DescriptorLayout;

    // Flags
    bool m_IsInitialized;
    bool m_DidResize;
    VkSampleCountFlagBits m_MSAASamples;

    // Counters
    size_t m_FrameNum;
    size_t m_AllocatedMaterials;

    // Cleanup
    DeletionQueue m_MainDeletionQueue;
    DeletionQueue m_DynamicDeletionQueue;

    // Data
    VkPresentModeKHR m_PresentMode;
    VkExtent2D m_Extent;
    VkViewport m_Viewport;
    VkRect2D m_Scissor;
    VkPipelineLayout m_PipelineLayout;
    VkPipeline m_Pipeline;
    VkPipeline m_TransparencyPipeline;
    VkPushConstantRange m_PushConstantRange;

    struct VmaAllocator_T *m_Allocator;

    UploadContext m_UploadContext;

    // Vulkan Primitives
    VkInstance m_Instance;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;
    VkSurfaceKHR m_Surface;
    VkDebugUtilsMessengerEXT m_DebugMessenger;
    VkRenderPass m_RenderPass;

    // Swapchain
    VkSwapchainKHR m_Swapchain;
    VkFormat m_SwapchainFormat;
    VkFormat m_DepthFormat;
    std::vector<SwapchainImageData> m_Images;

    // Synchronization
    FrameData m_Frames[FRAMES_IN_FLIGHT];
    VkQueue m_GraphicsQueue;
    uint32_t m_GraphicsQueueFamily;
};
