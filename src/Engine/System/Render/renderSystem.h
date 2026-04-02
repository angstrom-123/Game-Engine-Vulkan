#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "Component/material.h"
#include "Component/mesh.h"
#include "ECS/ecs.h"
#include "Util/allocator.h"
#include "Util/imageLoader.h"
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

class RenderSystem : public System {
public:
    void Init(struct GLFWwindow *window, Config *config);
    void SetCamera(Entity camera);
    void Cleanup();
    void Draw(ECS& ecs);
    void RequestResize();
    void AllocateMesh(Mesh& mesh);
    AllocatedImage AllocateImage(ImageData& imageData);
    void CreateMaterial(const MaterialInfo *info, Material& material);

    Signature GetSignature(ECS& ecs) { return ecs.GetBit<Transform>() | ecs.GetBit<Mesh>() | ecs.GetBit<Material>(); };
    size_t GetFrameNumber() { return m_FrameNum; };

private:
    void Resize(ECS& ecs);
    void InitVulkan(struct GLFWwindow *window);
    void InitSwapchain();
    void InitCommands();
    void InitDefaultRenderPass();
    void InitFramebuffers();
    void InitSyncStructures();
    void InitPipelines();
    void InitUniformBuffers();
    void ImmediateSubmit(std::function<void (VkCommandBuffer commandBuffer)>&& function);
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule *module);

private:
    Entity m_Camera;

    // Flags
    bool m_IsInitialized;
    bool m_DidResize;

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
    VkDescriptorSetLayout m_MaterialSetLayout;
    VkDeviceSize m_MinimumUniformOffset;

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
