struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> g_SourceColor : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(VertexOutput input) : SV_Target
{
    return g_SourceColor.Sample(g_Sampler, input.uv);
}
