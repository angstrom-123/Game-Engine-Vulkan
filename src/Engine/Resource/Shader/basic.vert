#version 450

layout (set = 0, binding = 0) uniform GlobalUniforms {
    mat4 vp;
    vec4 lightPos;
    vec4 viewPos;
} uniforms;

layout (push_constant) uniform Constants {
    mat4 model;
    uint ambientIndex;
    uint diffuseIndex;
    uint displacementIndex;
} constants;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout (location = 0) out vec3 vPosition;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec2 vUV;

void main()
{
    gl_Position = uniforms.vp * constants.model * vec4(aPosition, 1.0);

    vPosition = vec3(constants.model * vec4(aPosition, 1.0));
    vNormal = normalize(mat3(constants.model) * aNormal);
    vUV = aUV;
}
