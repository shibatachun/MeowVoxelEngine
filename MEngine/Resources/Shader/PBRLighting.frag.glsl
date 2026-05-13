#version 450

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 out_Color;

layout(binding = 0) uniform texture2D g_Position;
layout(binding = 1) uniform texture2D g_Normal;
layout(binding = 2) uniform texture2D g_Albedo;
layout(binding = 3) uniform texture2D g_Material;
layout(binding = 4) uniform sampler g_Sampler;

layout(binding = 5) uniform LightingConstants
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

const float PI = 3.14159265359;

float distributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.000001);
}

float geometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness)
{
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 evaluatePbrLight(vec3 albedo, float metallic, float roughness, vec3 normal, vec3 viewDirection, vec3 lightDirection, vec3 radiance)
{
    vec3 halfway = normalize(viewDirection + lightDirection);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = fresnelSchlick(max(dot(halfway, viewDirection), 0.0), f0);
    float normalDistribution = distributionGGX(normal, halfway, roughness);
    float geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
    vec3 specular = (normalDistribution * geometry * fresnel) /
        max(4.0 * max(dot(normal, viewDirection), 0.0) * max(dot(normal, lightDirection), 0.0), 0.0001);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * radiance * max(dot(normal, lightDirection), 0.0);
}

vec3 evaluateSkyIrradiance(vec3 normal)
{
    float upward = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 groundBounce = vec3(0.025, 0.023, 0.02);
    vec3 skyBounce = mix(vec3(0.08, 0.13, 0.22), vec3(0.24, 0.40, 0.72), upward);
    return mix(groundBounce, skyBounce, upward) * lighting.skyParameters.x * lighting.skyParameters.z;
}

vec3 tonemap(vec3 color)
{
    color = color / (color + vec3(1.0));
    return pow(color, vec3(1.0 / 2.2));
}

void main()
{
    vec4 packedPosition = texture(sampler2D(g_Position, g_Sampler), v_UV);
    if (packedPosition.a < 0.5) {
        discard;
    }

    vec3 worldPosition = packedPosition.xyz;
    vec3 normal = normalize(texture(sampler2D(g_Normal, g_Sampler), v_UV).xyz * 2.0 - 1.0);
    vec3 albedo = pow(texture(sampler2D(g_Albedo, g_Sampler), v_UV).rgb, vec3(2.2));
    vec4 material = texture(sampler2D(g_Material, g_Sampler), v_UV);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float ambientStrength = clamp(material.z, 0.0, 1.0);
    vec3 viewDirection = normalize(lighting.cameraPosition.xyz - worldPosition);

    vec3 color = ambientStrength * albedo * evaluateSkyIrradiance(normal);
    vec3 sunLightDirection = normalize(-lighting.sunDirection.xyz);
    color += evaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, sunLightDirection,
        lighting.sunColorAndIntensity.rgb * lighting.sunColorAndIntensity.a);

    int lightCount = int(clamp(lighting.pointLightCount.x, 0.0, 4.0));
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightVector = lighting.pointLightPosition[i].xyz - worldPosition;
        float distanceToLight = length(lightVector);
        vec3 lightDirection = lightVector / max(distanceToLight, 0.0001);
        float attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
        vec3 radiance = lighting.pointLightColorAndIntensity[i].rgb * lighting.pointLightColorAndIntensity[i].a * attenuation;
        color += evaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, lightDirection, radiance);
    }

    out_Color = vec4(tonemap(color), 1.0);
}
