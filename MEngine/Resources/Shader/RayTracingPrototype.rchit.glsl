#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 rtPayload;
hitAttributeEXT vec2 hitAttribs;

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

layout(set = 0, binding = 3, std430) readonly buffer VertexBuffer
{
    uint vertexData[];
} uVertices;

layout(set = 0, binding = 4, std430) readonly buffer IndexBuffer
{
    uint indices[];
} uIndices;

// Mirrors MEngine::Resources::MeshVertex; VulkanRenderer has a static_assert
// for this stride because the shader reads the raw buffer manually.
const uint VertexStrideBytes = 92u;
const uint VertexPositionOffsetBytes = 0u;
const uint VertexNormalOffsetBytes = 12u;
const uint VertexColorOffsetBytes = 24u;

float loadVertexFloat(uint vertexIndex, uint byteOffset)
{
    uint wordIndex = (vertexIndex * VertexStrideBytes + byteOffset) >> 2;
    return uintBitsToFloat(uVertices.vertexData[wordIndex]);
}

vec3 loadVertexVec3(uint vertexIndex, uint byteOffset)
{
    return vec3(
        loadVertexFloat(vertexIndex, byteOffset),
        loadVertexFloat(vertexIndex, byteOffset + 4u),
        loadVertexFloat(vertexIndex, byteOffset + 8u));
}

vec3 interpolateVertexVec3(uint i0, uint i1, uint i2, uint byteOffset, vec3 barycentrics)
{
    vec3 v0 = loadVertexVec3(i0, byteOffset);
    vec3 v1 = loadVertexVec3(i1, byteOffset);
    vec3 v2 = loadVertexVec3(i2, byteOffset);
    return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

void main()
{
    uint primitiveIndex = uint(gl_PrimitiveID);
    uint i0 = uIndices.indices[primitiveIndex * 3u + 0u];
    uint i1 = uIndices.indices[primitiveIndex * 3u + 1u];
    uint i2 = uIndices.indices[primitiveIndex * 3u + 2u];

    vec3 barycentrics = vec3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
    vec3 normal = normalize(interpolateVertexVec3(i0, i1, i2, VertexNormalOffsetBytes, barycentrics));
    vec3 albedo = clamp(interpolateVertexVec3(i0, i1, i2, VertexColorOffsetBytes, barycentrics), vec3(0.0), vec3(1.0));
    vec3 worldPosition = interpolateVertexVec3(i0, i1, i2, VertexPositionOffsetBytes, barycentrics);

    vec3 viewDir = normalize(uLighting.cameraPosition.xyz - worldPosition);
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }

    vec3 sunDir = normalize(-uLighting.sunDirection.xyz);
    vec3 sunColor = uLighting.sunColorAndIntensity.rgb * max(uLighting.sunColorAndIntensity.a, 0.0);
    float nDotL = max(dot(normal, sunDir), 0.0);
    float halfLambert = 0.35 + 0.65 * nDotL;
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0) * 0.25;
    vec3 ambient = albedo * vec3(0.12, 0.15, 0.18);
    vec3 direct = albedo * sunColor * halfLambert;
    rtPayload = vec4(ambient + direct + rim * vec3(0.45, 0.62, 0.85), 1.0);
}
