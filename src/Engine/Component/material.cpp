#include "material.h"

FragmentPushConstants::Material Material::Pack()
{
    return {
        .albedo = albedo,
        .roughness = roughness,
        .metallic = metallic,
        .ao = ao,
        .emissive = emissive,
        .diffuseIndex = diffuseTexture.arrayID,
        .diffuseLayer = diffuseTexture.layerID,
        .normalIndex = normalTexture.arrayID,
        .normalLayer = normalTexture.layerID,
        .flags = flags
    };
}
