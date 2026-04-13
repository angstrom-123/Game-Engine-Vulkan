#version 450

layout (push_constant) uniform Constants {
    mat4 model;
    mat4 vp;
} constants;

layout (location = 0) in vec3 aPosition;

void main()
{
    gl_Position = constants.vp * constants.model * vec4(aPosition, 1.0);
}
