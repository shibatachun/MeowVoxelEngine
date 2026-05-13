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
layout(binding = 2) uniform sampler g_Sampler;
layout(binding = 3, rgba16f) uniform writeonly image2D out_MultiScatteringLut;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_MultiScatteringLut);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec3 transmittance = texture(sampler2D(g_TransmittanceLut, g_Sampler), uv).rgb;
    float horizon = smoothstep(0.0, 1.0, uv.y);
    float phaseLift = mix(0.35, 1.0, horizon) * lighting.skyParameters.x;
    vec3 multipleScatter = (vec3(1.0) - transmittance) * vec3(0.38, 0.52, 0.72) * phaseLift;
    multipleScatter += lighting.sunColorAndIntensity.rgb * lighting.skyParameters.y * pow(uv.x, 10.0) * 0.05;

    imageStore(out_MultiScatteringLut, pixel, vec4(multipleScatter, 1.0));
}
