#version 450

#define CUTOUT_THRESHOLD 0.75

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
    uint diffuseIndex;
    uint diffuseLayer;
    uint normalIndex;
    uint normalLayer;
    uint flags;
} constants;

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    // Camera
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;
    float near;
    float far;
    // Settings
    float exposure;
    float gamma;
    float ambientIntensity;
    uint lightCount;
    ivec2 screenSize;
    // Light culling
    uint tileSize;
    uint tilesX;
    uint tilesY;
    uint maxLightsPerTile;
} uniforms;

layout (set = 1, binding = 0) uniform sampler2DArray textures[TEXTURE_ARRAY_COUNT];

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vUV;
layout (location = 2) in mat3 vTBN;

layout (location = 0) out vec4 outAlbedo;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outMaterial;

vec4 ComputeAlbedo()
{
    vec4 albedo = texture(textures[constants.diffuseIndex], vec3(vUV, constants.diffuseLayer));
    albedo.xyz *= constants.albedo.xyz;
    return albedo;
}

vec3 ComputeNormal()
{
    // TODO: Can go back to this if I fixup normal mipmapping
    // vec3 normal = texture(textures[constants.normalIndex], vec3(vUV, constants.normalLayer)).xyz;

    // Goofy manual lod selection to fix some visible triangle seams
    float dist = length(vPosition - uniforms.position.xyz);
    float lod = log2(dist * 0.8); // higher scaling factor = higher lod
    lod = clamp(lod, 0.0, textureQueryLevels(textures[constants.normalIndex]) - 1);

    vec3 normal = textureLod(textures[constants.normalIndex], vec3(vUV, constants.normalLayer), lod).xyz;

    normal = normal * 2.0 - 1.0;
    normal = normalize(vTBN * normal);
    return normal * 0.5 + 0.5;
}

vec4 ComputeMaterial()
{
    float roughness = constants.roughness;
    float metallic = constants.metallic;
    float ao = constants.ao;
    float emissive = constants.emissive;

    return vec4(roughness, metallic, ao, emissive);
}

void main()
{
    // Albedo
    vec4 albedo = ComputeAlbedo();
    if ((constants.flags & MATERIAL_FLAG_CUTOUT) == MATERIAL_FLAG_CUTOUT && albedo.a < CUTOUT_THRESHOLD) {
        discard;
    }
    outAlbedo = vec4(albedo.xyz, 1.0);

    // Normal
    vec3 normal = ComputeNormal();
    if ((constants.flags & MATERIAL_FLAG_DOUBLE_SIDED) == MATERIAL_FLAG_DOUBLE_SIDED && !gl_FrontFacing) {
        normal = -normal;
    }
    outNormal = vec4(normal, 0.0);

    // Material
    outMaterial = ComputeMaterial();
}
