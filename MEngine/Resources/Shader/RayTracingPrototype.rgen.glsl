#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT vec4 rtPayload;

layout(set = 0, binding = 0, std140) uniform LightingConstants
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
} uLighting;

layout(set = 0, binding = 1) uniform accelerationStructureEXT uScene;
layout(set = 0, binding = 2, rgba16f) uniform image2D uOutput;
layout(set = 0, binding = 5) uniform texture2D uRasterSkyWater;
layout(set = 0, binding = 6) uniform sampler uLinearSampler;

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    vec2 size = vec2(gl_LaunchSizeEXT.xy);
    vec2 uv = (vec2(pixel) + vec2(0.5)) / max(size, vec2(1.0));
    vec2 ndc = uv * 2.0 - 1.0;

    float aspect = max(uLighting.cameraParameters.y, 0.01);
    float tanHalfFov = max(uLighting.cameraParameters.x, 0.01);
    vec3 viewDir = normalize(
        uLighting.cameraForward.xyz +
        uLighting.cameraRight.xyz * ndc.x * aspect * tanHalfFov -
        uLighting.cameraUp.xyz * ndc.y * tanHalfFov);

    vec3 color = texture(sampler2D(uRasterSkyWater, uLinearSampler), uv).rgb;
    vec3 sunViewDir = normalize(-uLighting.sunDirection.xyz);

    float sunDot = max(dot(viewDir, sunViewDir), 0.0);
    float sunCore = pow(sunDot, 4096.0);
    float sunHalo = pow(sunDot, 64.0);
    float sunGlow = pow(sunDot, 12.0);
    vec3 sunColor = uLighting.sunColorAndIntensity.rgb * max(uLighting.sunColorAndIntensity.a, 0.0);
    color += sunColor * (sunCore * 5.0 + sunHalo * 0.8 + sunGlow * 0.08);

    rtPayload = vec4(color, 0.0);
    traceRayEXT(
        uScene,
        gl_RayFlagsOpaqueEXT,
        0xff,
        0,
        0,
        0,
        uLighting.cameraPosition.xyz,
        0.01,
        viewDir,
        10000.0,
        0);
    color = mix(color, rtPayload.rgb, rtPayload.a);

    float exposure = max(uLighting.skyParameters.z, 0.05);
    color = vec3(1.0) - exp(-color * exposure);
    imageStore(uOutput, pixel, vec4(color, 1.0));
}
