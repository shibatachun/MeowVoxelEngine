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

void main()
{
    ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
    ivec3 size = imageSize(out_CloudDensity);
    if (voxel.x >= size.x || voxel.y >= size.y || voxel.z >= size.z) {
        return;
    }

    vec3 uvw = (vec3(voxel) + 0.5) / vec3(size);
    vec3 period = vec3(8.0, 128.0, 8.0);
    vec2 windDirection = normalize(lighting.cloudWindParameters.xy + vec2(0.0001, 0.0));
    float windPhase = lighting.cloudWindParameters.w * lighting.cloudWindParameters.z;
    vec3 evolution = vec3(windDirection.x, 0.18, windDirection.y) * windPhase * 0.035;
    vec3 p = vec3(uvw.x * period.x, uvw.y * 2.2, uvw.z * period.z) + evolution;

    float height = uvw.y;
    float bottomFade = smoothstep(0.02, 0.16, height);
    float topFade = 1.0 - smoothstep(0.72, 1.0, height);
    float anvil = smoothstep(0.42, 0.82, height) * 0.12;
    float heightProfile = bottomFade * topFade;

    float baseShape = fbm(p, period);
    float billow = 1.0 - abs(valueNoise(p * 2.0 + 21.7, period * 2.0) * 2.0 - 1.0);
    float detail = fbm(p * 4.0 + vec3(31.4, 9.2, 18.5), period * 4.0);
    float coverage = clamp(lighting.cloudParameters.x, 0.0, 0.98);
    float coverageRemap = mix(0.86, 0.50, coverage);
    float density = smoothstep(coverageRemap, coverageRemap + 0.24, baseShape + billow * 0.18 + anvil);
    density *= heightProfile;
    density = max(density - detail * 0.36 * (1.0 - height * 0.35), 0.0);

    float erosion = clamp(detail * 1.45, 0.0, 1.0);
    float ambient = mix(0.45, 1.0, height);
    imageStore(out_CloudDensity, voxel, vec4(density, erosion, ambient, 1.0));
}
