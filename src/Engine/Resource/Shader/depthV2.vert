#version 450

layout (push_constant) uniform Constants {
    mat4 model;
} constants;

layout (set = 0, binding = 0) uniform PerFrameUniforms { 
    mat4 view;
    mat4 proj;
} uniforms;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUV;

layout (location = 0) out vec2 vUV;

void main()
{
    gl_Position = uniforms.proj * uniforms.view * constants.model * vec4(aPosition, 1.0);
    vUV = aUV;
}
