#version 450

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

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

layout(binding = 1, rgba16f) uniform writeonly image3D out_CloudDensity;

vec3 wrapGrid(vec3 p, vec3 period)
{
    return mod(p, period);
}

float hash31(vec3 p, vec3 period)
{
    p = wrapGrid(p, period);
    p = fract(p * vec3(127.1, 311.7, 74.7));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p, vec3 period)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float c000 = hash31(i + vec3(0.0, 0.0, 0.0), period);
    float c100 = hash31(i + vec3(1.0, 0.0, 0.0), period);
    float c010 = hash31(i + vec3(0.0, 1.0, 0.0), period);
    float c110 = hash31(i + vec3(1.0, 1.0, 0.0), period);
    float c001 = hash31(i + vec3(0.0, 0.0, 1.0), period);
    float c101 = hash31(i + vec3(1.0, 0.0, 1.0), period);
    float c011 = hash31(i + vec3(0.0, 1.0, 1.0), period);
    float c111 = hash31(i + vec3(1.0, 1.0, 1.0), period);

    float x00 = mix(c000, c100, u.x);
    float x10 = mix(c010, c110, u.x);
    float x01 = mix(c001, c101, u.x);
    float x11 = mix(c011, c111, u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

float fbm(vec3 p, vec3 period)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += valueNoise(p, period) * amplitude;
        p = p * 2.0 + vec3(13.1, 7.7, 17.3);
        period *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

float remap(float value, float oldMin, float oldMax, float newMin, float newMax)
{
    float normalized = clamp((value - oldMin) / max(oldMax - oldMin, 0.0001), 0.0, 1.0);
    return mix(newMin, newMax, normalized);
}

void main()
{
    ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
    ivec3 size = imageSize(out_CloudDensity);
    if (voxel.x >= size.x || voxel.y >= size.y || voxel.z >= size.z) {
        return;
    }

    vec3 uvw = (vec3(voxel) + 0.5) / vec3(size);
    vec3 period = vec3(7.0, 128.0, 7.0);
    vec2 windDirection = normalize(lighting.cloudWindParameters.xy + vec2(0.0001, 0.0));
    float windPhase = lighting.cloudWindParameters.w * lighting.cloudWindParameters.z;
    vec3 evolution = vec3(windDirection.x, 0.18, windDirection.y) * windPhase * 0.035;
    vec3 p = vec3(uvw.x * period.x, uvw.y * 2.6, uvw.z * period.z) + evolution;

    float height = uvw.y;
    float bottomFade = smoothstep(0.015, 0.10, height);
    float topFade = 1.0 - smoothstep(0.74, 1.0, height);
    float bodyProfile = bottomFade * topFade;
    float seaProfile = smoothstep(0.02, 0.14, height) * (1.0 - smoothstep(0.36, 0.70, height));
    float crownProfile = smoothstep(0.24, 0.58, height) * (1.0 - smoothstep(0.70, 1.0, height));

    float continent = fbm(vec3(p.xz * 0.34, 0.0).xzy + vec3(4.3, 0.0, 9.1), vec3(period.xz * 0.34, 8.0).xzy);
    float baseShape = fbm(p * vec3(0.82, 0.55, 0.82), period);
    float billow = 1.0 - abs(valueNoise(p * 2.2 + 21.7, period * 2.0) * 2.0 - 1.0);
    float towers = pow(clamp(fbm(p * vec3(1.4, 0.7, 1.4) + vec3(11.2, 3.0, 5.4), period * vec3(1.5, 1.0, 1.5)), 0.0, 1.0), 1.7);
    float detail = fbm(p * 4.8 + vec3(31.4, 9.2, 18.5), period * 4.0);
    float wisps = fbm(p * vec3(8.0, 1.2, 8.0) + vec3(7.0, 5.3, 2.8), period * vec3(8.0, 1.0, 8.0));
    float coverage = clamp(lighting.cloudParameters.x, 0.0, 0.98);
    float coverageRemap = mix(0.82, 0.42, coverage);

    float sheet = smoothstep(coverageRemap - 0.10, coverageRemap + 0.18, continent + baseShape * 0.32);
    float cauliflower = smoothstep(coverageRemap + 0.02, coverageRemap + 0.28, baseShape + billow * 0.26 + towers * 0.22);
    float density = sheet * seaProfile * 0.72 + cauliflower * bodyProfile * mix(0.65, 1.35, crownProfile);
    density *= remap(continent, coverageRemap - 0.20, 1.0, 0.28, 1.0);
    density = max(density - detail * mix(0.18, 0.42, height) - wisps * 0.10 * (1.0 - crownProfile), 0.0);
    density = clamp(density * mix(0.85, 1.28, coverage), 0.0, 1.0);

    float erosion = clamp(detail * 1.15 + wisps * 0.35, 0.0, 1.0);
    float ambient = mix(0.30, 1.0, height) * mix(0.72, 1.05, crownProfile);
    imageStore(out_CloudDensity, voxel, vec4(density, erosion, ambient, 1.0));
}
