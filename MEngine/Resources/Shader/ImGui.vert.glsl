#version 450

layout(push_constant) uniform PushConstants
{
    vec2 scale;
    vec2 translate;
} pc;

layout(location = 0) in vec2 in_Position;
layout(location = 1) in vec2 in_UV;
layout(location = 2) in vec4 in_Color;

layout(location = 0) out vec2 v_UV;
layout(location = 1) out vec4 v_Color;

void main()
{
    v_UV = in_UV;
    v_Color = in_Color;
    gl_Position = vec4(in_Position * pc.scale + pc.translate, 0.0, 1.0);
}
