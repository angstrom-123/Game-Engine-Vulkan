#version 450

layout (std140, binding = 0) uniform VertexUniform {
    vec4 placeholder;
} uniforms;

layout (push_constant) uniform Constants {
    mat4 mvp;
    mat4 model;
} constants;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout (location = 0) out vec3 vPosition;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec2 vUV;

void main()
{
    gl_Position = constants.mvp * vec4(aPosition, 1.0);

    vPosition = vec3(constants.model * vec4(aPosition, 1.0));
    vNormal = mat3(constants.model) * aNormal;
    vUV = aUV;
}
