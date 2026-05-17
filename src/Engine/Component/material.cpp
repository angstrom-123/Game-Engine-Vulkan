#include "material.h"

FragmentPushConstants::Material Material::Pack()
{
    return {
        .albedo = albedo,
        .roughness = roughness,
        .metallic = metallic,
        .ao = ao,
        .emissive = emissive,
        .diffuseTexture = (diffuseTexture.arrayID << 16) | diffuseTexture.layerID,
        .normalTexture = (normalTexture.arrayID << 16) | normalTexture.layerID,
        .flags = flags
    };
}
