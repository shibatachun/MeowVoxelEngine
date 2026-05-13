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

layout(binding = 1, rgba16f) uniform writeonly image2D out_TransmittanceLut;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(out_TransmittanceLut);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    float viewHeight = uv.y;
    float sunCosTheta = uv.x * 2.0 - 1.0;
    float opticalDepth = mix(1.9, 0.08, viewHeight);

    vec3 rayleighBeta = vec3(5.8, 13.5, 33.1) * 0.012 * lighting.skyParameters.x;
    vec3 mieBeta = vec3(1.0) * 0.018 * lighting.skyParameters.y;
    float horizonBoost = 1.0 + pow(1.0 - abs(sunCosTheta), 4.0) * 2.2;
    vec3 transmittance = exp(-(rayleighBeta + mieBeta) * opticalDepth * horizonBoost);

    imageStore(out_TransmittanceLut, pixel, vec4(transmittance, 1.0));
}
