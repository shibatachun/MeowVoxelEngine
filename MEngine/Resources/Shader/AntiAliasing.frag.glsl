#version 450

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 out_Color;

layout(binding = 0) uniform texture2D u_Color;
layout(binding = 1) uniform sampler u_Sampler;
layout(binding = 2) uniform texture2D u_History;

layout(push_constant) uniform AntiAliasingConstants
{
    vec4 inverseResolutionAndMode;
    vec4 temporalParameters;
} aa;

float luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 sampleCurrent(vec2 uv)
{
    return texture(sampler2D(u_Color, u_Sampler), uv).rgb;
}

vec3 runFxaa(vec2 uv, vec2 texel)
{
    vec3 center = sampleCurrent(uv);
    float lumaCenter = luma(center);
    float lumaNW = luma(sampleCurrent(uv + texel * vec2(-1.0, -1.0)));
    float lumaNE = luma(sampleCurrent(uv + texel * vec2(1.0, -1.0)));
    float lumaSW = luma(sampleCurrent(uv + texel * vec2(-1.0, 1.0)));
    float lumaSE = luma(sampleCurrent(uv + texel * vec2(1.0, 1.0)));

    float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        return center;
    }

    vec2 edgeDirection = vec2(
        -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
        ((lumaNW + lumaSW) - (lumaNE + lumaSE)));

    float directionReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float inverseDirectionAdjustment = 1.0 / (min(abs(edgeDirection.x), abs(edgeDirection.y)) + directionReduce);
    edgeDirection = clamp(edgeDirection * inverseDirectionAdjustment, vec2(-8.0), vec2(8.0)) * texel;

    vec3 sampleA = 0.5 * (
        sampleCurrent(uv + edgeDirection * (1.0 / 3.0 - 0.5)) +
        sampleCurrent(uv + edgeDirection * (2.0 / 3.0 - 0.5)));
    vec3 sampleB = sampleA * 0.5 + 0.25 * (
        sampleCurrent(uv + edgeDirection * -0.5) +
        sampleCurrent(uv + edgeDirection * 0.5));

    float lumaB = luma(sampleB);
    return (lumaB < lumaMin || lumaB > lumaMax) ? sampleA : sampleB;
}

vec3 runTaa(vec2 uv)
{
    vec3 current = sampleCurrent(uv);
    if (aa.inverseResolutionAndMode.w < 0.5) {
        return current;
    }

    vec3 history = texture(sampler2D(u_History, u_Sampler), uv).rgb;
    float historyWeight = clamp(aa.temporalParameters.x, 0.0, 0.95);
    return mix(current, history, historyWeight);
}

void main()
{
    // Offscreen render targets are sampled with the opposite vertical origin
    // from the swapchain presentation path used by the previous direct pass.
    vec2 uv = vec2(v_UV.x, 1.0 - v_UV.y);
    vec2 texel = aa.inverseResolutionAndMode.xy;
    int mode = int(aa.inverseResolutionAndMode.z + 0.5);

    vec3 color = sampleCurrent(uv);
    if (mode == 1) {
        color = runFxaa(uv, texel);
    } else if (mode == 2) {
        color = runTaa(uv);
    }

    out_Color = vec4(color, 1.0);
}
