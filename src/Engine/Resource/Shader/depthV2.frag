#version 450

#define ALPHA_CUTOUT_THRESHOLD 0.75

#define MATERIAL_FLAG_NONE 0
#define MATERIAL_FLAG_TRANSPARENT 1
#define MATERIAL_FLAG_DOUBLE_SIDED 2
#define MATERIAL_FLAG_CUTOUT 4

#define TEXTURE_ARRAY_COUNT 5

layout (push_constant) uniform Constants {
    layout (offset = 64) vec4 albedo; // w = alpha
    float roughness;
    float metallic;
    float ao;
    float emissive;
    uint diffuseTexture;
    uint normalTexture;
    uint flags;
} constants;

layout (set = 3, binding = 0) uniform sampler2DArray textures[TEXTURE_ARRAY_COUNT];

layout (location = 0) in vec2 vUV;

void main()
{
    if ((constants.flags & MATERIAL_FLAG_CUTOUT) == MATERIAL_FLAG_CUTOUT) {
        float alpha = texture(textures[constants.diffuseTexture >> 16], vec3(vUV, constants.diffuseTexture & 0xFFFF)).a;
        if (alpha < ALPHA_CUTOUT_THRESHOLD) {
            discard;
        }
    }
}
