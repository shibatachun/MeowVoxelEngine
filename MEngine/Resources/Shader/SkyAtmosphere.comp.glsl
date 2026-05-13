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

layout(binding = 1) uniform texture2D g_TransmittanceLut;
layout(binding = 2) uniform texture2D g_MultiScatteringLut;
layout(binding = 3) uniform sampler g_Sampler;
layout(binding = 4, rgba16f) uniform writeonly image2D out_SkyAtmosphere;

vec3 evaluateSkyAtmosphere(vec3 rayDirection)
{
    vec3 sunDirection = normalize(-lighting.sunDirection.xyz);
    float sunAmount = max(dot(rayDirection, sunDirection), 0.0);
    float horizon = clamp(rayDirection.y * 0.5 + 0.5, 0.0, 1.0);
    float rayleigh = pow(max(rayDirection.y, 0.0), 0.45) * lighting.skyParameters.x;
    float mie = pow(sunAmount, 48.0) * lighting.skyParameters.y;
    float sunDisk = smoothstep(0.9993, 0.99985, sunAmount);
    float innerHalo = pow(sunAmount, 384.0);
    float outerHalo = pow(sunAmount, 18.0);
    float glare = smoothstep(0.88, 1.0, sunAmount);

    vec3 deepSky = vec3(0.05, 0.20, 0.55);
    vec3 zenithSky = vec3(0.18, 0.45, 1.0);
    vec3 horizonSky = vec3(0.75, 0.86, 1.0);
    vec3 sky = mix(horizonSky, mix(deepSky, zenithSky, rayleigh), horizon);

    vec2 lutUv = vec2(sunAmount, horizon);
    vec3 transmittance = texture(sampler2D(g_TransmittanceLut, g_Sampler), lutUv).rgb;
    vec3 multiScattering = texture(sampler2D(g_MultiScatteringLut, g_Sampler), lutUv).rgb;
    sky = sky * transmittance + multiScattering;
    vec3 sunColor = lighting.sunColorAndIntensity.rgb * lighting.sunColorAndIntensity.a;
    sky += sunColor * (mie * 0.35 + outerHalo * 0.55 + innerHalo * 1.4 + sunDisk * 10.0);
    sky += sunColor * glare * glare * vec3(1.0, 0.88, 0.62) * 0.25;
    return sky * lighting.skyParameters.z;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_SkyAtmosphere);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 rayDirection = normalize(
        lighting.cameraForward.xyz +
        lighting.cameraRight.xyz * ndc.x * lighting.cameraParameters.x * lighting.cameraParameters.y -
        lighting.cameraUp.xyz * -ndc.y * lighting.cameraParameters.x);

    imageStore(out_SkyAtmosphere, pixel, vec4(evaluateSkyAtmosphere(rayDirection), 1.0));
}
