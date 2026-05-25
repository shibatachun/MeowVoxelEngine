#version 450

layout(location = 0) in vec3 in_Position;

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection;
    vec4 materialParameters;
} constants;

void main()
{
    gl_Position = constants.viewProjection * vec4(in_Position, 1.0);
}
