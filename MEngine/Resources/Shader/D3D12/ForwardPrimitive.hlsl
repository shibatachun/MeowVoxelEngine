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
    float3 normal : TEXCOORD0;
    float3 color : TEXCOORD1;
};

cbuffer CameraConstants : register(b0)
{
    float4x4 viewProjection;
    float4 lightDirection;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.normal = normalize(input.normal);
    output.color = saturate(input.color);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    float ndotl = saturate(dot(normalize(input.normal), -normalize(lightDirection.xyz)));
    float lighting = 0.28 + ndotl * 0.72;
    return float4(input.color * lighting, 1.0);
}
