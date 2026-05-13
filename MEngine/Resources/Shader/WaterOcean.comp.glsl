#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform LightingConstants
{
    vec4 cameraPosition;
    vec4 cameraForward;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 sunDirection;
    vec4 sunColorAndIntensity;
    vec4 pointLightCount;
    vec4 pointLightPosition[4];
    vec4 pointLightColorAndIntensity[4];
    vec4 skyParameters;
    vec4 cloudParameters;
    vec4 cloudWindParameters;
    vec4 waterParameters;
    vec4 waterWindParameters;
    vec4 cameraParameters;
} lighting;

layout(binding = 1) uniform texture2D g_SkyClouds;
layout(binding = 2) uniform texture2D g_Position;
layout(binding = 3) uniform texture2D g_Normal;
layout(binding = 4) uniform texture2D g_Albedo;
layout(binding = 5) uniform texture2D g_Material;
layout(binding = 6) uniform sampler g_Sampler;
layout(binding = 7, rgba16f) uniform writeonly image2D out_WaterOcean;

const float PI = 3.14159265359;
const float Gravity = 9.81;

vec2 rotate(vec2 value, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return vec2(value.x * c - value.y * s, value.x * s + value.y * c);
}

vec3 directionToScreenUv(vec3 direction)
{
    float forward = dot(direction, lighting.cameraForward.xyz);
    if (forward <= 0.001) {
        return vec3(0.5, 0.5, 0.0);
    }

    float x = dot(direction, lighting.cameraRight.xyz) / max(forward, 0.001);
    float y = dot(direction, lighting.cameraUp.xyz) / max(forward, 0.001);
    vec2 ndc = vec2(
        x / max(lighting.cameraParameters.x * lighting.cameraParameters.y, 0.001),
        y / max(lighting.cameraParameters.x, 0.001));
    return vec3(ndc * 0.5 + 0.5, 1.0);
}

vec3 sampleSky(vec3 direction)
{
    vec3 uvw = directionToScreenUv(direction);
    if (uvw.z > 0.5 && all(greaterThanEqual(uvw.xy, vec2(0.0))) && all(lessThanEqual(uvw.xy, vec2(1.0)))) {
        return texture(sampler2D(g_SkyClouds, g_Sampler), uvw.xy).rgb;
    }

    float horizon = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.62, 0.73, 0.82), vec3(0.11, 0.33, 0.68), horizon) * lighting.skyParameters.z;
}

float hash11(float value)
{
    return fract(sin(value * 127.1) * 43758.5453);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float rippleNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec3 oceanNormal(vec2 position)
{
    vec2 wind = normalize(lighting.waterWindParameters.xy + vec2(0.0001, 0.0));
    float windSpeed = max(lighting.waterWindParameters.z, 0.1);
    float time = lighting.waterWindParameters.w;
    float amplitudeScale = lighting.waterParameters.y;
    float choppiness = lighting.waterParameters.z;

    float height = 0.0;
    vec2 gradient = vec2(0.0);

    for (int i = 0; i < 12; ++i) {
        float fi = float(i);
        float angle = (hash11(fi + 3.1) - 0.5) * 1.25;
        vec2 dir = normalize(rotate(wind, angle));
        float wavelength = mix(3.0, 42.0, pow((fi + 1.0) / 12.0, 1.85));
        float k = 2.0 * PI / wavelength;
        float omega = sqrt(Gravity * k) * mix(0.55, 1.45, windSpeed / 30.0);
        float directional = pow(max(dot(dir, wind), 0.0), 4.0);
        float spectrum = exp(-1.0 / max(k * k * windSpeed * windSpeed * 0.035, 0.0001)) / max(k * k * k * k, 0.0001);
        float amp = amplitudeScale * 0.018 * sqrt(spectrum) * directional / (1.0 + fi * 0.12);
        float phase = k * dot(dir, position) - omega * time + hash11(fi + 9.7) * PI * 2.0;
        float wave = sin(phase);
        float crest = cos(phase);

        height += wave * amp;
        gradient += dir * (crest * amp * k * choppiness);
    }

    gradient += vec2(
        sin(position.y * 0.17 + time * 1.8),
        cos(position.x * 0.19 + time * 1.6)) * amplitudeScale * 0.015;

    vec2 rippleP = position * 1.55 + wind * time * windSpeed * 0.32;
    vec2 ripple = vec2(
        rippleNoise(rippleP + vec2(0.0, 11.3)) - rippleNoise(rippleP + vec2(2.7, 0.0)),
        rippleNoise(rippleP + vec2(19.1, 5.4)) - rippleNoise(rippleP + vec2(4.2, 17.6)));
    vec2 fineP = position * 4.8 - rotate(wind, 1.1) * time * windSpeed * 0.55;
    ripple += vec2(sin(fineP.x + fineP.y), cos(fineP.x * 0.8 - fineP.y)) * 0.22;
    gradient += ripple * amplitudeScale * 0.045;
    return normalize(vec3(-gradient.x, 1.0, -gradient.y));
}

float oceanHeight(vec2 position)
{
    vec2 wind = normalize(lighting.waterWindParameters.xy + vec2(0.0001, 0.0));
    float time = lighting.waterWindParameters.w;
    float windSpeed = max(lighting.waterWindParameters.z, 0.1);
    float amplitudeScale = lighting.waterParameters.y;
    float height = 0.0;

    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        vec2 dir = normalize(rotate(wind, (hash11(fi + 3.1) - 0.5) * 1.25));
        float wavelength = mix(4.0, 38.0, pow((fi + 1.0) / 8.0, 1.7));
        float k = 2.0 * PI / wavelength;
        float omega = sqrt(Gravity * k) * mix(0.55, 1.45, windSpeed / 30.0);
        float amp = amplitudeScale * mix(0.022, 0.18, 1.0 / (fi + 1.0));
        height += sin(k * dot(dir, position) - omega * time + hash11(fi + 9.7) * PI * 2.0) * amp;
    }
    return height;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_WaterOcean);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec3 skyColor = texture(sampler2D(g_SkyClouds, g_Sampler), uv).rgb;
    vec4 packedScenePosition = texture(sampler2D(g_Position, g_Sampler), uv);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rayDirection = normalize(
        lighting.cameraForward.xyz +
        lighting.cameraRight.xyz * ndc.x * lighting.cameraParameters.x * lighting.cameraParameters.y -
        lighting.cameraUp.xyz * -ndc.y * lighting.cameraParameters.x);

    float waterLevel = lighting.waterParameters.x;
    if (packedScenePosition.a < 0.5 || packedScenePosition.y >= waterLevel || rayDirection.y >= -0.001 || lighting.waterParameters.y <= 0.001) {
        imageStore(out_WaterOcean, pixel, vec4(skyColor, 1.0));
        return;
    }

    float t = (waterLevel - lighting.cameraPosition.y) / rayDirection.y;
    float sceneDistance = length(packedScenePosition.xyz - lighting.cameraPosition.xyz);
    if (t <= 0.0 || t > 1800.0 || t >= sceneDistance) {
        imageStore(out_WaterOcean, pixel, vec4(skyColor, 1.0));
        return;
    }

    vec3 hit = lighting.cameraPosition.xyz + rayDirection * t;
    hit.y += oceanHeight(hit.xz);
    vec3 normal = oceanNormal(hit.xz);
    vec3 viewDirection = normalize(lighting.cameraPosition.xyz - hit);
    vec3 sunDirection = normalize(-lighting.sunDirection.xyz);
    vec3 reflected = reflect(-viewDirection, normal);

    vec2 refractionOffset = normal.xz * mix(0.012, 0.045, clamp(lighting.waterParameters.y, 0.0, 1.0)) / max(1.0 + t * 0.015, 1.0);
    vec2 refractedUv = clamp(uv + refractionOffset, vec2(0.001), vec2(0.999));
    vec4 refractedPosition = texture(sampler2D(g_Position, g_Sampler), refractedUv);
    vec3 bottomColor = pow(texture(sampler2D(g_Albedo, g_Sampler), refractedUv).rgb, vec3(2.2));
    if (refractedPosition.a < 0.5 || refractedPosition.y > waterLevel) {
        refractedPosition = packedScenePosition;
        bottomColor = pow(texture(sampler2D(g_Albedo, g_Sampler), uv).rgb, vec3(2.2));
    }

    float waterDepth = clamp(waterLevel - refractedPosition.y, 0.0, 18.0);
    vec3 bottomNormal = normalize(texture(sampler2D(g_Normal, g_Sampler), refractedUv).xyz * 2.0 - 1.0);
    float bottomLight = mix(0.45, 1.0, clamp(dot(bottomNormal, normalize(-lighting.sunDirection.xyz)) * 0.5 + 0.5, 0.0, 1.0));
    vec3 absorption = exp(-vec3(0.16, 0.045, 0.022) * waterDepth);

    float distanceFade = smoothstep(0.0, 360.0, t);
    float fresnel = pow(1.0 - clamp(dot(normal, viewDirection), 0.0, 1.0), 5.0);
    fresnel = mix(0.035, 1.0, fresnel);

    vec3 deepWater = vec3(0.003, 0.075, 0.12);
    vec3 shallowWater = vec3(0.04, 0.38, 0.42);
    vec3 waterTint = mix(shallowWater, deepWater, smoothstep(0.0, 12.0, waterDepth));
    vec3 refractedColor = bottomColor * bottomLight * absorption + waterTint * (1.0 - absorption);
    vec3 reflection = sampleSky(reflected);

    float nDotL = max(dot(normal, sunDirection), 0.0);
    vec3 halfway = normalize(viewDirection + sunDirection);
    float tightSparkle = pow(max(dot(normal, halfway), 0.0), 620.0) * (0.35 + length(normal.xz) * 1.7);
    float sunSpec = pow(max(dot(reflected, sunDirection), 0.0), mix(520.0, 120.0, lighting.waterParameters.y));
    float broadGlint = pow(max(dot(reflected, sunDirection), 0.0), 18.0) * 0.16;
    float caustic = pow(0.5 + 0.5 * sin(hit.x * 2.7 + hit.z * 1.9 + lighting.waterWindParameters.w * 4.0), 5.0);
    caustic *= smoothstep(0.0, 2.5, waterDepth) * (1.0 - smoothstep(5.0, 14.0, waterDepth));
    float shoreline = 1.0 - smoothstep(0.0, 0.9, waterDepth);
    float foam = max(smoothstep(0.18, 0.42, length(normal.xz)) * (1.0 - distanceFade * 0.75), shoreline * 0.32);

    vec3 sunColor = lighting.sunColorAndIntensity.rgb * lighting.sunColorAndIntensity.a;
    vec3 color = mix(refractedColor, reflection, fresnel * 0.88);
    color += waterTint * nDotL * 0.18;
    color += vec3(0.35, 0.85, 0.95) * caustic * 0.18;
    color += sunColor * (sunSpec * 3.5 + broadGlint + tightSparkle * 1.2);
    color = mix(color, vec3(0.90, 0.98, 1.0), foam * 0.28);
    color = mix(color, skyColor, smoothstep(420.0, 900.0, t) * 0.55);

    imageStore(out_WaterOcean, pixel, vec4(color, 1.0));
}
