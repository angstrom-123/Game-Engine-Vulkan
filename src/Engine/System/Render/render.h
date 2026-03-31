#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "Engine/Component/material.h"
#include "Engine/Component/mesh.h"
#include "Engine/ECS/ecs.h"
#include "Engine/Util/allocator.h"
#include "Engine/config.h"

#include "renderTypes.h"

struct DeletionQueue {
    std::deque<std::function<void ()>> deletors;

    void PushFunction(std::function<void ()> function) { deletors.push_back(function); }
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

class RenderSystem : public System {
public:
    void Init(ECS& ecs, struct GLFWwindow *window, Config *config);
    void Cleanup();
    void Draw(ECS& ecs);
    void RequestResize();
    void AllocateMesh(Mesh& mesh);
    Material CreateMaterial(const MaterialInfo *info);

    Signature GetSignature(ECS *ecs) { return (Signature) { ecs->GetBit<Transform>() | ecs->GetBit<Mesh>() | ecs->GetBit<Material>() }; };
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
    void LoadShaderModule(const std::filesystem::path& path, VkShaderModule *module);

private:
    Entity m_Camera;
    size_t m_FrameNum;
    bool m_Initialized;
    bool m_DidResize;
    size_t m_AllocatedMaterials;
    DeletionQueue m_MainDeletionQueue;
    DeletionQueue m_DynamicDeletionQueue;

    VkPresentModeKHR m_PresentMode;
    VkExtent2D m_Extent;
    VkViewport m_Viewport;
    VkRect2D m_Scissor;

    struct VmaAllocator_T *m_Allocator;

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
    std::vector<ImageData> m_Images;

    // Synchronization
    FrameData m_Frames[FRAMES_IN_FLIGHT];
    VkQueue m_GraphicsQueue;
    uint32_t m_GraphicsQueueFamily;
};
