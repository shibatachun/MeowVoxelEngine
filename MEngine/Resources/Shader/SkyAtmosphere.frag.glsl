#version 450

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 out_Color;

layout(binding = 0) uniform texture2D g_AtmosphereTexture;
layout(binding = 1) uniform sampler g_Sampler;

void main()
{
    out_Color = texture(sampler2D(g_AtmosphereTexture, g_Sampler), v_UV);
}
