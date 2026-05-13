#version 450

layout(set = 0, binding = 0) uniform texture2D fontTexture;
layout(set = 0, binding = 1) uniform sampler fontSampler;

layout(location = 0) in vec2 v_UV;
layout(location = 1) in vec4 v_Color;
layout(location = 0) out vec4 out_Color;

void main()
{
    float fontAlpha = texture(sampler2D(fontTexture, fontSampler), v_UV).r;
    out_Color = vec4(v_Color.rgb, v_Color.a * fontAlpha);
}
