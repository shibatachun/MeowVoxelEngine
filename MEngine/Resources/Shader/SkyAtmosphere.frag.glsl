#version 450

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 out_Color;

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
    vec4 cameraParameters;
} lighting;

vec3 evaluateSkyAtmosphere(vec3 rayDirection)
{
    vec3 sunDirection = normalize(-lighting.sunDirection.xyz);
    float sunAmount = max(dot(rayDirection, sunDirection), 0.0);
    float horizon = clamp(rayDirection.y * 0.5 + 0.5, 0.0, 1.0);
    float rayleigh = pow(max(rayDirection.y, 0.0), 0.45) * lighting.skyParameters.x;
    float mie = pow(sunAmount, 48.0) * lighting.skyParameters.y;
    float sunDisk = smoothstep(0.9993, 0.99985, sunAmount);

    vec3 deepSky = vec3(0.05, 0.20, 0.55);
    vec3 zenithSky = vec3(0.18, 0.45, 1.0);
    vec3 horizonSky = vec3(0.75, 0.86, 1.0);
    vec3 sky = mix(horizonSky, mix(deepSky, zenithSky, rayleigh), horizon);
    sky += lighting.sunColorAndIntensity.rgb * (mie * 0.35 + sunDisk * 6.0) * lighting.sunColorAndIntensity.a;
    return sky * lighting.skyParameters.z;
}

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p)
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

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += valueNoise(p) * amplitude;
        p = p * 2.03 + vec2(17.3, 9.2);
        amplitude *= 0.5;
    }
    return value;
}

float cloudDensityAt(vec3 worldPosition, float cloudBase, float cloudTop)
{
    float height01 = clamp((worldPosition.y - cloudBase) / max(cloudTop - cloudBase, 0.001), 0.0, 1.0);
    float heightFade = smoothstep(0.03, 0.22, height01) * (1.0 - smoothstep(0.72, 1.0, height01));
    vec2 cloudUv = worldPosition.xz * 0.035 + vec2(worldPosition.y * 0.012, -worldPosition.y * 0.007);
    float baseShape = fbm(cloudUv);
    float detail = fbm(cloudUv * 3.4 + 31.7) * 0.32;
    float coverage = lighting.cloudParameters.x;
    float density = clamp((baseShape + detail - coverage) / max(1.0 - coverage, 0.08), 0.0, 1.0);
    return density * heightFade * lighting.cloudParameters.y;
}

vec4 evaluateVolumetricClouds(vec3 rayOrigin, vec3 rayDirection, vec3 skyColor)
{
    float cloudBase = lighting.cloudParameters.z;
    float cloudTop = cloudBase + max(lighting.cloudParameters.w, 0.1);
    if (lighting.cloudParameters.y <= 0.001 || rayDirection.y <= 0.001) {
        return vec4(skyColor, 0.0);
    }

    float t0 = (cloudBase - rayOrigin.y) / rayDirection.y;
    float t1 = (cloudTop - rayOrigin.y) / rayDirection.y;
    if (t1 < 0.0) {
        return vec4(skyColor, 0.0);
    }

    float startT = max(min(t0, t1), 0.0);
    float endT = max(t0, t1);
    float rayLength = min(endT - startT, 180.0);
    if (rayLength <= 0.001) {
        return vec4(skyColor, 0.0);
    }

    vec3 sunDirection = normalize(-lighting.sunDirection.xyz);
    float stepSize = rayLength / 16.0;
    float transmittance = 1.0;
    vec3 scattered = vec3(0.0);

    for (int i = 0; i < 16; ++i) {
        float t = startT + (float(i) + 0.5) * stepSize;
        vec3 position = rayOrigin + rayDirection * t;
        float density = cloudDensityAt(position, cloudBase, cloudTop);
        if (density <= 0.001) {
            continue;
        }

        float shadowDensity = 0.0;
        for (int j = 1; j <= 3; ++j) {
            vec3 shadowPosition = position + sunDirection * (float(j) * 2.5);
            shadowDensity += cloudDensityAt(shadowPosition, cloudBase, cloudTop);
        }

        float sunVisibility = exp(-shadowDensity * 0.55);
        float powder = 1.0 - exp(-density * 2.0);
        vec3 cloudLight = mix(vec3(0.42, 0.48, 0.56), lighting.sunColorAndIntensity.rgb, sunVisibility);
        cloudLight *= mix(0.45, 1.35, max(dot(rayDirection, sunDirection), 0.0)) * powder;

        float alpha = 1.0 - exp(-density * stepSize * 0.13);
        scattered += transmittance * alpha * cloudLight;
        transmittance *= 1.0 - alpha;
        if (transmittance < 0.04) {
            break;
        }
    }

    float alpha = 1.0 - transmittance;
    vec3 color = skyColor * transmittance + scattered;
    return vec4(color, alpha);
}

vec3 tonemap(vec3 color)
{
    color = color / (color + vec3(1.0));
    return pow(color, vec3(1.0 / 2.2));
}

void main()
{
    vec2 ndc = v_UV * 2.0 - 1.0;
    vec3 rayDirection = normalize(
        lighting.cameraForward.xyz +
        lighting.cameraRight.xyz * ndc.x * lighting.cameraParameters.x * lighting.cameraParameters.y -
        lighting.cameraUp.xyz * -ndc.y * lighting.cameraParameters.x);

    vec3 sky = evaluateSkyAtmosphere(rayDirection);
    vec4 clouds = evaluateVolumetricClouds(lighting.cameraPosition.xyz, rayDirection, sky);
    out_Color = vec4(tonemap(clouds.rgb), 1.0);
}
