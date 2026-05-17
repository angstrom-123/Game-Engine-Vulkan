#version 450

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    // Camera
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;
    // Settings
    float exposure;
    float gamma;
    float ambientIntensity;
    uint lightCount;
    ivec2 screenSize;
} uniforms;

layout (set = 0, binding = 1) uniform sampler2D hdrTexture;

layout (location = 0) in vec2 vUV;

layout (location = 0) out vec4 outColor;

vec3 ToneMapACES(vec3 color)
{
    color *= uniforms.exposure;

    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;

    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3(0.0), vec3(1.0));
}

vec3 GammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0 / uniforms.gamma));
}

vec3 LinearToSRGB(vec3 color)
{
    vec3 sRGB;
    bvec3 cut = lessThan(color, vec3(0.0031308));
    sRGB.r = cut.r ? color.r * 12.92 : 1.055 * pow(color.r, 1.0 / 2.4) - 0.055;
    sRGB.g = cut.g ? color.g * 12.92 : 1.055 * pow(color.g, 1.0 / 2.4) - 0.055;
    sRGB.b = cut.b ? color.b * 12.92 : 1.055 * pow(color.b, 1.0 / 2.4) - 0.055;
    return sRGB;
}

void main()
{
    vec3 hdr = texture(hdrTexture, vUV).rgb;

    vec3 ldr;
    ldr = ToneMapACES(hdr);
    ldr = GammaCorrect(ldr);

    outColor = vec4(ldr, 1.0);
}
