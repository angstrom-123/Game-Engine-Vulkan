#include "material.h"

FragmentPushConstants::Material Material::Pack()
{
    return {
        .albedo = albedo,
        .roughness = roughness,
        .metallic = metallic,
        .ao = ao,
        .emissive = emissive,
        .diffuseIndex = static_cast<uint32_t>(diffuseTexture.arrayID),
        .diffuseLayer = diffuseTexture.layerID,
        .normalIndex = static_cast<uint32_t>(normalTexture.arrayID),
        .normalLayer = normalTexture.layerID,
        .flags = flags
    };
}
