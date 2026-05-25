struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> g_Position : register(t0);
Texture2D<float4> g_Normal : register(t1);
Texture2D<float4> g_Albedo : register(t2);
Texture2D<float4> g_Material : register(t3);
Texture2D<float> g_CharacterShadow : register(t4);
SamplerState g_Sampler : register(s0);

cbuffer LightingConstants : register(b0)
{
    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 sunDirection;
    float4 sunColorAndIntensity;
    float4 pointLightCount;
    float4 pointLightPosition[4];
    float4 pointLightColorAndIntensity[4];
    float4 skyParameters;
    float4 cloudParameters;
    float4 cloudWindParameters;
    float4 waterParameters;
    float4 waterWindParameters;
    float4 cameraParameters;
    float4 actorShadowParameters;
    float4x4 shadowLightViewProjection;
};

static const float PI = 3.14159265359;

float DistributionGGX(float3 normal, float3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness) *
        GeometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 EvaluatePbrLight(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance)
{
    float3 halfway = normalize(viewDirection + lightDirection);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 fresnel = FresnelSchlick(max(dot(halfway, viewDirection), 0.0), f0);
    float normalDistribution = DistributionGGX(normal, halfway, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float3 specular = (normalDistribution * geometry * fresnel) /
        max(4.0 * max(dot(normal, viewDirection), 0.0) * max(dot(normal, lightDirection), 0.0), 0.0001);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * radiance * max(dot(normal, lightDirection), 0.0);
}

float3 EvaluateSkyIrradiance(float3 normal)
{
    float upward = saturate(normal.y * 0.5 + 0.5);
    float3 groundBounce = float3(0.025, 0.023, 0.02);
    float3 skyBounce = lerp(float3(0.08, 0.13, 0.22), float3(0.24, 0.40, 0.72), upward);
    return lerp(groundBounce, skyBounce, upward) * skyParameters.x * skyParameters.z;
}

float3 Tonemap(float3 color)
{
    color = color / (color + float3(1.0, 1.0, 1.0));
    return pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float4 PSMain(VertexOutput input) : SV_Target
{
    float4 packedPosition = g_Position.Sample(g_Sampler, input.uv);
    if (packedPosition.a < 0.5) {
        discard;
    }

    float3 worldPosition = packedPosition.xyz;
    float3 normal = normalize(g_Normal.Sample(g_Sampler, input.uv).xyz * 2.0 - 1.0);
    float3 albedo = pow(g_Albedo.Sample(g_Sampler, input.uv).rgb, float3(2.2, 2.2, 2.2));
    float4 material = g_Material.Sample(g_Sampler, input.uv);
    float metallic = saturate(material.x);
    float roughness = clamp(material.y, 0.04, 1.0);
    float ambientStrength = saturate(material.z);
    float3 viewDirection = normalize(cameraPosition.xyz - worldPosition);
    float3 sunLightDirection = normalize(-sunDirection.xyz);

    float3 color = ambientStrength * albedo * EvaluateSkyIrradiance(normal);
    color += EvaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, sunLightDirection,
        sunColorAndIntensity.rgb * sunColorAndIntensity.a);

    uint lightCount = (uint)clamp(pointLightCount.x, 0.0, 4.0);
    for (uint i = 0; i < lightCount; ++i) {
        float3 lightVector = pointLightPosition[i].xyz - worldPosition;
        float distanceToLight = length(lightVector);
        float3 lightDirection = lightVector / max(distanceToLight, 0.0001);
        float attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
        float3 radiance = pointLightColorAndIntensity[i].rgb * pointLightColorAndIntensity[i].a * attenuation;
        color += EvaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, lightDirection, radiance);
    }

    return float4(Tonemap(color), 1.0);
}
