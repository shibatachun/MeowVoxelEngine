struct VertexInput
{
    float3 position : POSITION;
};

cbuffer ShadowConstants : register(b0)
{
    float4x4 viewProjection;
    float4 materialParameters;
};

float4 VSMain(VertexInput input) : SV_Position
{
    return mul(float4(input.position, 1.0), viewProjection);
}

void PSMain()
{
}
