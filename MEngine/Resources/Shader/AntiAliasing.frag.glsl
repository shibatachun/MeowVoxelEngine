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
    vec4 lensFlareSun;
    vec4 lensFlareColor;
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

float flareDisc(vec2 uv, vec2 center, float radius, float softness)
{
    float distanceToCenter = length(uv - center);
    return exp(-abs(distanceToCenter - radius) * softness);
}

float edgeFade(vec2 uv, float border)
{
    vec2 fade = smoothstep(vec2(0.0), vec2(border), uv) *
        smoothstep(vec2(0.0), vec2(border), vec2(1.0) - uv);
    return fade.x * fade.y;
}

vec3 spectralTint(float t)
{
    return clamp(vec3(
        1.15 - abs(t - 0.18) * 3.0,
        1.05 - abs(t - 0.48) * 2.7,
        1.15 - abs(t - 0.78) * 3.0), vec3(0.0), vec3(1.0));
}

vec3 evaluateLensFlare(vec2 uv)
{
    float visible = aa.lensFlareSun.z;
    float intensity = aa.lensFlareSun.w;
    if (visible <= 0.001 || intensity <= 0.001) {
        return vec3(0.0);
    }

    vec2 sunUv = aa.lensFlareSun.xy;
    float aspect = max(aa.inverseResolutionAndMode.y / max(aa.inverseResolutionAndMode.x, 0.000001), 0.001);
    vec2 sunToPixel = (uv - sunUv) * vec2(aspect, 1.0);
    float sunDistance = length(sunToPixel);
    float onScreenFade = smoothstep(1.28, 0.46, length(sunUv - vec2(0.5)));
    float nearEdgeFade = smoothstep(0.0, 0.24, min(min(sunUv.x, 1.0 - sunUv.x), min(sunUv.y, 1.0 - sunUv.y)));
    float screenFade = onScreenFade * mix(0.35, 1.0, nearEdgeFade);
    float strength = visible * intensity * screenFade;

    vec3 sunColor = aa.lensFlareColor.rgb;
    vec3 flare = vec3(0.0);
    float pixelEdgeFade = edgeFade(uv, 0.08);
    float artifactGuard = smoothstep(0.0, 0.18, length(vec2(0.5) - sunUv));

    float hotCore = exp(-sunDistance * 38.0);
    float softBloom = exp(-sunDistance * 8.0);
    float wideVeil = exp(-sunDistance * 2.1);
    flare += sunColor * (hotCore * 2.2 + softBloom * 0.65 + wideVeil * 0.10);

    float horizontalRay = pow(max(1.0 - abs(sunToPixel.y) * 120.0, 0.0), 2.0) * exp(-abs(sunToPixel.x) * 2.6);
    float verticalRay = pow(max(1.0 - abs(sunToPixel.x) * 155.0, 0.0), 2.0) * exp(-abs(sunToPixel.y) * 5.2);
    float diagonalA = pow(max(1.0 - abs(dot(sunToPixel, normalize(vec2(0.72, -0.69)))) * 95.0, 0.0), 2.2) * exp(-sunDistance * 2.3);
    float diagonalB = pow(max(1.0 - abs(dot(sunToPixel, normalize(vec2(0.72, 0.69)))) * 125.0, 0.0), 2.0) * exp(-sunDistance * 3.6);
    flare += sunColor * (horizontalRay * 0.85 + verticalRay * 0.28 + diagonalA * 0.42 + diagonalB * 0.20);

    vec2 axis = vec2(0.5) - sunUv;
    vec2 ghost0 = sunUv + axis * 0.42;
    vec2 ghost1 = sunUv + axis * 0.72;
    vec2 ghost2 = sunUv + axis * 1.08;
    vec2 ghost3 = sunUv + axis * 1.38;

    flare += vec3(0.55, 0.86, 1.0) * exp(-length((uv - ghost0) * vec2(aspect, 1.0)) * 48.0) * 0.25 * edgeFade(ghost0, 0.18);
    flare += vec3(1.0, 0.92, 0.72) * exp(-length((uv - ghost1) * vec2(aspect, 1.0)) * 64.0) * 0.20 * edgeFade(ghost1, 0.18);
    flare += vec3(0.66, 0.92, 1.0) * exp(-length((uv - ghost2) * vec2(aspect, 1.0)) * 42.0) * 0.20 * edgeFade(ghost2, 0.18);

    vec2 rainbowUv = (uv - ghost3) * vec2(aspect, 1.0);
    float rainbowRadius = length(rainbowUv);
    float rainbowRing = exp(-abs(rainbowRadius - 0.17) * 30.0) * smoothstep(0.02, 0.11, rainbowRadius);
    float rainbowAngle = atan(rainbowUv.y, rainbowUv.x) * 0.15915494 + 0.5;
    flare += spectralTint(rainbowAngle) * rainbowRing * 0.13 * edgeFade(ghost3, 0.24) * artifactGuard;

    float apertureRing = flareDisc(uv * vec2(aspect, 1.0), sunUv * vec2(aspect, 1.0), 0.13, 54.0);
    flare += vec3(1.0, 0.58, 0.28) * apertureRing * 0.055;

    return flare * strength * pixelEdgeFade;
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

    color += evaluateLensFlare(uv);
    out_Color = vec4(color, 1.0);
}
