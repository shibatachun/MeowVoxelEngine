#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform SkinningConstants
{
    mat4 fitTransform;
    uint vertexCount;
    uint jointCount;
    uint _pad0;
    uint _pad1;
} skinning;

layout(set = 0, binding = 1, std430) readonly buffer SourceVertices
{
    uint words[];
} sourceVertices;

layout(set = 0, binding = 2, std430) readonly buffer SkinningMatrices
{
    uint words[];
} skinningMatrices;

layout(set = 0, binding = 3, std430) buffer OutputVertices
{
    uint words[];
} outputVertices;

const uint VertexStrideBytes = 92u;
const uint VertexStrideWords = VertexStrideBytes / 4u;
const uint PositionOffsetWords = 0u;
const uint NormalOffsetWords = 3u;
const uint BoneIndexOffsetWords = 15u;
const uint BoneWeightOffsetWords = 19u;
const uint Mat4StrideWords = 16u;

float loadVertexFloat(uint vertexIndex, uint wordOffset)
{
    return uintBitsToFloat(sourceVertices.words[vertexIndex * VertexStrideWords + wordOffset]);
}

uint loadVertexUint(uint vertexIndex, uint wordOffset)
{
    return sourceVertices.words[vertexIndex * VertexStrideWords + wordOffset];
}

vec3 loadVertexVec3(uint vertexIndex, uint wordOffset)
{
    return vec3(
        loadVertexFloat(vertexIndex, wordOffset),
        loadVertexFloat(vertexIndex, wordOffset + 1u),
        loadVertexFloat(vertexIndex, wordOffset + 2u));
}

mat4 loadSkinningMatrix(uint jointIndex)
{
    uint base = jointIndex * Mat4StrideWords;
    return mat4(
        uintBitsToFloat(skinningMatrices.words[base + 0u]),
        uintBitsToFloat(skinningMatrices.words[base + 1u]),
        uintBitsToFloat(skinningMatrices.words[base + 2u]),
        uintBitsToFloat(skinningMatrices.words[base + 3u]),
        uintBitsToFloat(skinningMatrices.words[base + 4u]),
        uintBitsToFloat(skinningMatrices.words[base + 5u]),
        uintBitsToFloat(skinningMatrices.words[base + 6u]),
        uintBitsToFloat(skinningMatrices.words[base + 7u]),
        uintBitsToFloat(skinningMatrices.words[base + 8u]),
        uintBitsToFloat(skinningMatrices.words[base + 9u]),
        uintBitsToFloat(skinningMatrices.words[base + 10u]),
        uintBitsToFloat(skinningMatrices.words[base + 11u]),
        uintBitsToFloat(skinningMatrices.words[base + 12u]),
        uintBitsToFloat(skinningMatrices.words[base + 13u]),
        uintBitsToFloat(skinningMatrices.words[base + 14u]),
        uintBitsToFloat(skinningMatrices.words[base + 15u]));
}

void storeOutputVec3(uint vertexIndex, uint wordOffset, vec3 value)
{
    uint base = vertexIndex * VertexStrideWords + wordOffset;
    outputVertices.words[base + 0u] = floatBitsToUint(value.x);
    outputVertices.words[base + 1u] = floatBitsToUint(value.y);
    outputVertices.words[base + 2u] = floatBitsToUint(value.z);
}

void main()
{
    uint vertexIndex = gl_GlobalInvocationID.x;
    if (vertexIndex >= skinning.vertexCount) {
        return;
    }

    uint base = vertexIndex * VertexStrideWords;
    for (uint word = 0u; word < VertexStrideWords; ++word) {
        outputVertices.words[base + word] = sourceVertices.words[base + word];
    }

    vec4 position = vec4(loadVertexVec3(vertexIndex, PositionOffsetWords), 1.0);
    vec3 normal = loadVertexVec3(vertexIndex, NormalOffsetWords);

    vec4 skinnedPosition = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);
    float totalWeight = 0.0;
    for (uint slot = 0u; slot < 4u; ++slot) {
        uint jointIndex = loadVertexUint(vertexIndex, BoneIndexOffsetWords + slot);
        float weight = loadVertexFloat(vertexIndex, BoneWeightOffsetWords + slot);
        if (weight <= 0.0 || jointIndex >= skinning.jointCount) {
            continue;
        }

        mat4 skin = loadSkinningMatrix(jointIndex);
        skinnedPosition += (skin * position) * weight;
        skinnedNormal += (mat3(skin) * normal) * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0) {
        position = skinnedPosition / totalWeight;
        normal = skinnedNormal / totalWeight;
    }

    vec3 fittedPosition = (skinning.fitTransform * position).xyz;
    vec3 fittedNormal = normalize(mat3(skinning.fitTransform) * normal);
    storeOutputVec3(vertexIndex, PositionOffsetWords, fittedPosition);
    storeOutputVec3(vertexIndex, NormalOffsetWords, fittedNormal);
}
