#include "mipGenerator.h"

#include "backendDefs.h"
#include "device.h"

MipGenerator::MipGenerator(const Device& device, uint32_t maxMips)
{
    m_MaxMips = maxMips;
    InitDescriptor(device);
    InitPipeline(device);
}

void MipGenerator::Cleanup(const Device& device)
{
    m_Pipeline.Cleanup();
    vkDestroyDescriptorSetLayout(device.GetDevice(), m_DescriptorLayout, nullptr);
    vkDestroyDescriptorPool(device.GetDevice(), m_DescriptorPool, nullptr);
}

void MipGenerator::WriteDescriptors(const Device& device, VkDescriptorImageInfo *imageInfos)
{
    VkWriteDescriptorSet writes[2] = {
        { // Source
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_DescriptorSet,
            .dstBinding = 0,
            .descriptorCount = m_MaxMips,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = imageInfos
        },
        { // Destination
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_DescriptorSet,
            .dstBinding = 1,
            .descriptorCount = m_MaxMips,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = imageInfos
        }
    };
    vkUpdateDescriptorSets(device.GetDevice(), 2, writes, 0, nullptr);
}

void MipGenerator::Generate(VkCommandBuffer commandBuffer, uint32_t srcIndex, glm::uvec2 srcSize, uint32_t dstIndex, glm::uvec2 dstSize)
{
    if (!m_Ready) {
        m_Pipeline.BeginCompute(commandBuffer, { &m_DescriptorSet, 1 });
        m_Ready = true;
    }

    MipmapPushConstants constants = {
        .srcIndex = srcIndex,
        .srcWidth = srcSize.x,
        .srcHeight = srcSize.y,
        .dstIndex = dstIndex,
        .dstWidth = dstSize.x,
        .dstHeight = dstSize.y
    };
    vkCmdPushConstants(commandBuffer, 
            m_Pipeline.GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 
            0, sizeof(MipmapPushConstants), 
            &constants);

    const uint32_t TILE_SIZE = 16;
    uint32_t groupsX = (dstSize.x + TILE_SIZE - 1) / TILE_SIZE;
    uint32_t groupsY = (dstSize.y + TILE_SIZE - 1) / TILE_SIZE;
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);
}

void MipGenerator::Finalize(VkCommandBuffer commandBuffer)
{
    m_Pipeline.EndCompute();
    m_Ready = false;
}

void MipGenerator::InitDescriptor(const Device& device)
{
    VkDescriptorPoolSize poolSizes[1] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_MaxMips * 2 },    // Normal mipmap sources and destinations
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = poolSizes,
    };
    VK_CHECK(vkCreateDescriptorPool(device.GetDevice(), &poolInfo, nullptr, &m_DescriptorPool));

    // ================================================== Create Normal Mipmap Set  ==================================================

    {
        VkDescriptorSetLayoutBinding bindings[2] = {
            { // Source image 
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = m_MaxMips,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            { // Destination image 
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = m_MaxMips,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = bindings
        };
        VK_CHECK(vkCreateDescriptorSetLayout(device.GetDevice(), &layoutInfo, nullptr, &m_DescriptorLayout));

        // Allocate
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_DescriptorLayout
        };
        VK_CHECK(vkAllocateDescriptorSets(device.GetDevice(), &allocInfo, &m_DescriptorSet));
    }
}

void MipGenerator::InitPipeline(const Device& device)
{
    m_Pipeline.SetName("Mipmap")
            .SetDevice(device.GetDevice())
            .SetKind(PipelineKind::COMPUTE)
            .AddDescriptorLayout(m_DescriptorLayout)
            .AddPushConstant<MipmapPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
            .AddShader(ShaderKind::COMPUTE, "normalMipmap.comp")
            .Build();
}
