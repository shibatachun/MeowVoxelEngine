struct MeshVertex
{
    float3 position;
    float3 normal;
    float3 color;
    float2 texCoord;
    uint4 joints;
    float4 weights;
};

cbuffer SkinningConstants : register(b0)
{
    float4x4 fitTransform;
    uint vertexCount;
    uint jointCount;
    uint2 padding;
};

StructuredBuffer<MeshVertex> g_SourceVertices : register(t0);
StructuredBuffer<float4x4> g_SkinningMatrices : register(t1);
RWStructuredBuffer<MeshVertex> g_TargetVertices : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint vertexIndex = dispatchThreadId.x;
    if (vertexIndex >= vertexCount) {
        return;
    }

    MeshVertex vertex = g_SourceVertices[vertexIndex];
    float4 skinnedPosition = float4(0.0, 0.0, 0.0, 0.0);
    float3 skinnedNormal = float3(0.0, 0.0, 0.0);

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        uint jointIndex = vertex.joints[i];
        float weight = vertex.weights[i];
        if (weight > 0.0 && jointIndex < jointCount) {
            float4x4 jointMatrix = g_SkinningMatrices[jointIndex];
            skinnedPosition += mul(float4(vertex.position, 1.0), jointMatrix) * weight;
            skinnedNormal += mul(float4(vertex.normal, 0.0), jointMatrix).xyz * weight;
        }
    }

    if (dot(skinnedPosition, skinnedPosition) <= 0.000001) {
        skinnedPosition = float4(vertex.position, 1.0);
        skinnedNormal = vertex.normal;
    }

    vertex.position = mul(skinnedPosition, fitTransform).xyz;
    vertex.normal = normalize(mul(float4(skinnedNormal, 0.0), fitTransform).xyz);
    g_TargetVertices[vertexIndex] = vertex;
}
