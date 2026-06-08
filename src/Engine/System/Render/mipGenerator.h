#pragma once 

#include "pipeline.h"
#include <glm/ext/vector_uint2.hpp>
#include <vulkan/vulkan_core.h>

struct MipmapPushConstants {
    uint32_t srcIndex{0};
    uint32_t srcWidth{0};
    uint32_t srcHeight{0};
    uint32_t dstIndex{0};
    uint32_t dstWidth{0};
    uint32_t dstHeight{0};
};

class MipGenerator {
public:
    ~MipGenerator() = default;
    MipGenerator() = default;
    MipGenerator(const class Device& device, uint32_t maxMips);
    void Cleanup(const class Device& device);
    void WriteDescriptors(const class Device& device, VkDescriptorImageInfo *imageInfos);
    void Generate(VkCommandBuffer commandBuffer, uint32_t srcIndex, glm::uvec2 srcSize, uint32_t dstIndex, glm::uvec2 dstSize);
    void Finalize(VkCommandBuffer commandBuffer);

    VkDescriptorSetLayout GetDescriptorLayout() { return m_DescriptorLayout; }
    VkDescriptorSet GetDescriptorSet() { return m_DescriptorSet; }

private:
    void InitDescriptor(const class Device& device);
    void InitPipeline(const class Device& device);

private:
    bool m_Ready{false};
    uint32_t m_MaxMips{0};
    VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorSet m_DescriptorSet{VK_NULL_HANDLE};
    Pipeline m_Pipeline;
};
