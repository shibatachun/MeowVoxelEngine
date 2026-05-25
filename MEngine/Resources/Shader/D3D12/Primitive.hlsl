struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 faceUv : TEXCOORD;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 albedo : TEXCOORD2;
    float2 faceUv : TEXCOORD3;
};

cbuffer CameraConstants : register(b0)
{
    float4x4 viewProjection;
    float4 materialParameters;
};

Texture2D<float4> g_BaseColorTexture : register(t0);
SamplerState g_BaseColorSampler : register(s0);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.worldPosition = input.position;
    output.normal = normalize(input.normal);
    output.albedo = input.color;
    output.faceUv = input.faceUv;
    return output;
}

struct GBufferOutput
{
    float4 worldPosition : SV_Target0;
    float4 normal : SV_Target1;
    float4 albedo : SV_Target2;
    float4 material : SV_Target3;
};

GBufferOutput PSMain(VertexOutput input)
{
    float2 edgeDistance = min(input.faceUv, 1.0 - input.faceUv);
    float nearestEdge = min(edgeDistance.x, edgeDistance.y);
    float edgeWidth = 0.035;
    float edgeSoftness = max(fwidth(nearestEdge), 0.006);
    float edgeMask = 1.0 - smoothstep(edgeWidth, edgeWidth + edgeSoftness, nearestEdge);
    float disablePrimitiveEdges = saturate(materialParameters.w);
    float3 textureAlbedo = g_BaseColorTexture.Sample(g_BaseColorSampler, input.faceUv).rgb;
    float3 baseAlbedo = textureAlbedo * saturate(input.albedo);
    float3 edgedAlbedo = lerp(baseAlbedo, float3(0.015, 0.015, 0.015), edgeMask * 0.82 * (1.0 - disablePrimitiveEdges));

    GBufferOutput output;
    output.worldPosition = float4(input.worldPosition, 1.0);
    output.normal = float4(normalize(input.normal) * 0.5 + 0.5, 1.0);
    output.albedo = float4(edgedAlbedo, 1.0);
    output.material = float4(
        saturate(materialParameters.x),
        clamp(materialParameters.y, 0.04, 1.0),
        saturate(materialParameters.z),
        1.0);
    return output;
}
