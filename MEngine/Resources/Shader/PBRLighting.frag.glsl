#version 450

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 out_Color;

layout(binding = 0) uniform texture2D g_Position;
layout(binding = 1) uniform texture2D g_Normal;
layout(binding = 2) uniform texture2D g_Albedo;
layout(binding = 3) uniform texture2D g_Material;
layout(binding = 4) uniform sampler g_Sampler;
layout(binding = 6) uniform texture2D g_CharacterShadow;

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
    vec4 cloudWindParameters;
    vec4 waterParameters;
    vec4 waterWindParameters;
    vec4 cameraParameters;
    vec4 actorShadowParameters;
    mat4 shadowLightViewProjection;
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

float sampleShadowPcf(vec2 shadowUv, float receiverDepth, float bias)
{
    vec2 texelSize = vec2(1.0) / vec2(textureSize(sampler2D(g_CharacterShadow, g_Sampler), 0));
    float shadow = 0.0;
    float weight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = shadowUv + vec2(x, y) * texelSize * 1.25;
            if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) {
                continue;
            }
            float kernelWeight = 1.0 - length(vec2(x, y)) * 0.12;
            float closestDepth = texture(sampler2D(g_CharacterShadow, g_Sampler), sampleUv).r;
            shadow += (receiverDepth - bias > closestDepth ? 1.0 : 0.0) * kernelWeight;
            weight += kernelWeight;
        }
    }

    return weight > 0.0 ? shadow / weight : 0.0;
}

float sampleCharacterShadow(vec3 worldPosition, vec3 normal, vec3 sunLightDirection)
{
    if (lighting.actorShadowParameters.w < 0.5) {
        return 0.0;
    }

    vec4 shadowClip = lighting.shadowLightViewProjection * vec4(worldPosition, 1.0);
    if (shadowClip.w <= 0.0) {
        return 0.0;
    }

    vec3 shadowCoord = shadowClip.xyz / shadowClip.w;
    vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 0.0;
    }

    float normalBias = 1.0 - clamp(dot(normal, sunLightDirection), 0.0, 1.0);
    float bias = mix(0.0025, 0.009, normalBias);
    float unflippedShadow = sampleShadowPcf(shadowUv, shadowCoord.z, bias);
    float flippedShadow = sampleShadowPcf(vec2(shadowUv.x, 1.0 - shadowUv.y), shadowCoord.z, bias);
    return max(unflippedShadow, flippedShadow);
}

float sampleProjectedActorShadow(vec3 worldPosition, vec3 normal)
{
    if (lighting.actorShadowParameters.w < 0.5 || normal.y < 0.18) {
        return 0.0;
    }

    vec3 actorBase = lighting.actorShadowParameters.xyz;
    vec3 lightRay = normalize(lighting.sunDirection.xyz);
    if (lightRay.y >= -0.04) {
        return 0.0;
    }

    vec2 rayXZ = lightRay.xz;
    float rayLength = length(rayXZ);
    vec2 along = rayLength > 0.0001 ? rayXZ / rayLength : vec2(1.0, 0.0);
    vec2 side = vec2(-along.y, along.x);

    float shadow = 0.0;
    for (int i = 0; i < 7; ++i) {
        float normalizedIndex = float(i) / 6.0;
        float height = mix(0.15, 1.95, normalizedIndex);
        vec3 casterPoint = actorBase + vec3(0.0, height, 0.0);
        float t = (worldPosition.y - casterPoint.y) / lightRay.y;
        if (t <= 0.0) {
            continue;
        }

        vec3 projectedPoint = casterPoint + lightRay * t;
        vec2 delta = worldPosition.xz - projectedPoint.xz;
        float axial = dot(delta, along);
        float lateral = dot(delta, side);
        float torsoWeight = 1.0 - abs(normalizedIndex - 0.46) * 1.35;
        float width = mix(0.28, 0.72, clamp(torsoWeight, 0.0, 1.0));
        float lengthScale = mix(0.42, 1.34, normalizedIndex);
        float footprint = sqrt((lateral * lateral) / (width * width) +
            (axial * axial) / (lengthScale * lengthScale));
        float heightWeight = mix(0.72, 1.0, clamp(torsoWeight, 0.0, 1.0));
        shadow = max(shadow, (1.0 - smoothstep(0.72, 1.14, footprint)) * heightWeight);
    }

    float contact = 1.0 - smoothstep(0.28, 0.86, distance(worldPosition.xz, actorBase.xz));
    float contactHeight = 1.0 - smoothstep(0.0, 0.65, abs(worldPosition.y - actorBase.y));
    float receiverFade = 1.0 - smoothstep(3.8, 7.2, distance(worldPosition.xz, actorBase.xz));
    float heightFade = 1.0 - smoothstep(0.0, 2.6, actorBase.y - worldPosition.y);
    float surfaceFade = smoothstep(0.18, 0.72, normal.y);
    return max(shadow * receiverFade * heightFade * 0.78, contact * contactHeight * 0.48) * surfaceFade;
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
    float waterLevel = lighting.waterParameters.x;
    bool cameraUnderwater = lighting.cameraPosition.y < waterLevel;
    bool fragmentUnderwater = worldPosition.y < waterLevel;
    if (!cameraUnderwater && fragmentUnderwater) {
        discard;
    }

    vec3 normal = normalize(texture(sampler2D(g_Normal, g_Sampler), v_UV).xyz * 2.0 - 1.0);
    vec3 albedo = pow(texture(sampler2D(g_Albedo, g_Sampler), v_UV).rgb, vec3(2.2));
    vec4 material = texture(sampler2D(g_Material, g_Sampler), v_UV);
    float metallic = clamp(material.x, 0.0, 1.0);
    float roughness = clamp(material.y, 0.04, 1.0);
    float ambientStrength = clamp(material.z, 0.0, 1.0);
    vec3 viewDirection = normalize(lighting.cameraPosition.xyz - worldPosition);

    vec3 color = ambientStrength * albedo * evaluateSkyIrradiance(normal);
    vec3 sunLightDirection = normalize(-lighting.sunDirection.xyz);
    float characterShadow = max(
        sampleCharacterShadow(worldPosition, normal, sunLightDirection),
        sampleProjectedActorShadow(worldPosition, normal));
    color += evaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, sunLightDirection,
        lighting.sunColorAndIntensity.rgb * lighting.sunColorAndIntensity.a) * (1.0 - characterShadow * 0.88);

    int lightCount = int(clamp(lighting.pointLightCount.x, 0.0, 4.0));
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightVector = lighting.pointLightPosition[i].xyz - worldPosition;
        float distanceToLight = length(lightVector);
        vec3 lightDirection = lightVector / max(distanceToLight, 0.0001);
        float attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
        vec3 radiance = lighting.pointLightColorAndIntensity[i].rgb * lighting.pointLightColorAndIntensity[i].a * attenuation;
        color += evaluatePbrLight(albedo, metallic, roughness, normal, viewDirection, lightDirection, radiance);
    }

    color *= 1.0 - characterShadow * 0.16;

    if (cameraUnderwater && fragmentUnderwater) {
        float viewDistance = length(worldPosition - lighting.cameraPosition.xyz);
        vec3 waterTint = vec3(0.02, 0.28, 0.36);
        vec3 absorption = exp(-vec3(0.18, 0.06, 0.025) * viewDistance);
        float haze = 1.0 - exp(-viewDistance * 0.08);
        color = color * absorption + waterTint * haze * lighting.skyParameters.z;
    }

    out_Color = vec4(tonemap(color), 1.0);
}
