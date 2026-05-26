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

layout(binding = 1) uniform texture2D g_SkyAtmosphere;
layout(binding = 2) uniform texture3D g_CloudDensity;
layout(binding = 3) uniform sampler g_LinearSampler;
layout(binding = 4, rgba16f) uniform writeonly image2D out_VolumetricClouds;

const float CloudMapWorldSize = 420.0;

vec3 tonemap(vec3 color)
{
    color = color / (color + vec3(1.0));
    return pow(color, vec3(1.0 / 2.2));
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float henyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / max(pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5) * 12.56637, 0.0001);
}

vec3 cloudUvFromWorld(vec3 worldPosition, float cloudBase, float cloudTop)
{
    float height01 = clamp((worldPosition.y - cloudBase) / max(cloudTop - cloudBase, 0.001), 0.0, 1.0);
    vec2 windDirection = normalize(lighting.cloudWindParameters.xy + vec2(0.0001, 0.0));
    vec2 wind = windDirection * lighting.cloudWindParameters.z * lighting.cloudWindParameters.w / CloudMapWorldSize;
    vec2 uv = worldPosition.xz / CloudMapWorldSize + wind;
    return vec3(uv.x, height01, uv.y);
}

vec4 sampleCloudDensity(vec3 worldPosition, float cloudBase, float cloudTop)
{
    vec3 uvw = cloudUvFromWorld(worldPosition, cloudBase, cloudTop);
    vec4 volume = texture(sampler3D(g_CloudDensity, g_LinearSampler), uvw);
    float baseFade = smoothstep(0.0, 0.065, uvw.y);
    float topFade = 1.0 - smoothstep(0.82, 1.0, uvw.y);
    float heightFade = baseFade * topFade;
    float seaBoost = 1.0 + (1.0 - smoothstep(0.24, 0.58, uvw.y)) * 0.55;
    float density = volume.r * heightFade * seaBoost * lighting.cloudParameters.y * 0.95;
    density = max(density - volume.g * mix(0.045, 0.16, uvw.y), 0.0);
    return vec4(density, volume.gba);
}

float marchLight(vec3 position, vec3 sunDirection, float cloudBase, float cloudTop)
{
    float opticalDepth = 0.0;
    float stepLength = max(lighting.cloudParameters.w, 0.1) / 8.0;
    for (int i = 1; i <= 8; ++i) {
        vec3 samplePosition = position + sunDirection * (float(i) * stepLength);
        if (samplePosition.y < cloudBase || samplePosition.y > cloudTop) {
            continue;
        }
        opticalDepth += sampleCloudDensity(samplePosition, cloudBase, cloudTop).r * stepLength;
    }
    return exp(-opticalDepth * 0.36);
}

bool intersectCloudLayer(vec3 rayOrigin, vec3 rayDirection, float cloudBase, float cloudTop, out float startT, out float endT)
{
    if (abs(rayDirection.y) < 0.0001) {
        if (rayOrigin.y < cloudBase || rayOrigin.y > cloudTop) {
            return false;
        }
        startT = 0.0;
        endT = 260.0;
        return true;
    }

    float t0 = (cloudBase - rayOrigin.y) / rayDirection.y;
    float t1 = (cloudTop - rayOrigin.y) / rayDirection.y;
    startT = max(min(t0, t1), 0.0);
    endT = max(t0, t1);
    return endT > 0.0 && endT > startT;
}

vec4 evaluateVolumetricClouds(vec3 rayOrigin, vec3 rayDirection, vec3 skyColor)
{
    float cloudBase = lighting.cloudParameters.z;
    float cloudTop = cloudBase + max(lighting.cloudParameters.w, 0.1);
    if (lighting.cloudParameters.y <= 0.001) {
        return vec4(skyColor, 0.0);
    }

    float startT = 0.0;
    float endT = 0.0;
    if (!intersectCloudLayer(rayOrigin, rayDirection, cloudBase, cloudTop, startT, endT)) {
        return vec4(skyColor, 0.0);
    }

    float rayLength = min(endT - startT, 340.0);
    if (rayLength <= 0.001) {
        return vec4(skyColor, 0.0);
    }

    vec3 sunDirection = normalize(-lighting.sunDirection.xyz);
    float horizonFade = smoothstep(0.012, 0.12, abs(rayDirection.y));
    float viewSun = dot(rayDirection, sunDirection);
    float forwardPhase = henyeyGreenstein(viewSun, 0.58);
    float backwardPhase = henyeyGreenstein(viewSun, -0.22) * 0.30;
    float silverLining = henyeyGreenstein(viewSun, 0.84) * 0.42;
    float phase = max(forwardPhase + backwardPhase + silverLining, 0.020);

    vec2 jitterUv = fract(vec2(gl_GlobalInvocationID.xy) * vec2(0.75487767, 0.56984029));
    float jitter = hash12(jitterUv + vec2(rayDirection.x, rayDirection.z));
    float stepSize = rayLength / 72.0;
    float transmittance = 1.0;
    vec3 scattered = vec3(0.0);

    for (int i = 0; i < 72; ++i) {
        float t = startT + (float(i) + jitter) * stepSize;
        vec3 position = rayOrigin + rayDirection * t;
        vec4 densitySample = sampleCloudDensity(position, cloudBase, cloudTop);
        float density = densitySample.r * horizonFade;
        if (density <= 0.001) {
            continue;
        }

        float lightVisibility = marchLight(position, sunDirection, cloudBase, cloudTop);
        float height01 = clamp((position.y - cloudBase) / max(cloudTop - cloudBase, 0.001), 0.0, 1.0);
        float beer = exp(-density * stepSize * 0.13);
        float powder = 1.0 - exp(-density * mix(1.6, 2.8, height01));
        float alpha = clamp(1.0 - beer, 0.0, 1.0);

        vec3 coolBase = vec3(0.42, 0.50, 0.58) * skyColor;
        vec3 ambient = mix(coolBase, skyColor, mix(0.16, 0.62, densitySample.b));
        ambient *= mix(0.48, 1.08, height01);
        vec3 sunLight = lighting.sunColorAndIntensity.rgb * lighting.sunColorAndIntensity.a * lightVisibility * phase * powder * 7.5;
        vec3 cloudLight = ambient + sunLight;

        scattered += transmittance * alpha * cloudLight;
        transmittance *= 1.0 - alpha;
        if (transmittance < 0.018) {
            break;
        }
    }

    float cloudAlpha = 1.0 - transmittance;
    vec3 distantHaze = skyColor * smoothstep(140.0, 340.0, rayLength) * cloudAlpha * 0.18;
    vec3 color = skyColor * transmittance + scattered + distantHaze;
    return vec4(color, 1.0 - transmittance);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_VolumetricClouds);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rayDirection = normalize(
        lighting.cameraForward.xyz +
        lighting.cameraRight.xyz * ndc.x * lighting.cameraParameters.x * lighting.cameraParameters.y -
        lighting.cameraUp.xyz * -ndc.y * lighting.cameraParameters.x);

    vec3 sky = texture(sampler2D(g_SkyAtmosphere, g_LinearSampler), uv).rgb;
    vec4 clouds = evaluateVolumetricClouds(lighting.cameraPosition.xyz, rayDirection, sky);
    imageStore(out_VolumetricClouds, pixel, vec4(tonemap(clouds.rgb), 1.0));
}
