#version 450

// layout (binding = 0) uniform UBO {
// } ubo;

layout (push_constant) uniform Constants {
    mat4 mvp;
} constants;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout (location = 0) out vec3 vColor;

void main()
{
    gl_Position = constants.mvp * vec4(aPosition, 1.0);
    vColor = vec3(aUV, 0.5);
    // vColor = vec3(0.0, 1.0, 0.0);
}
