#define RootSig \
    "RootFlags(0), " \
    "DescriptorTable(CBV(b0), UAV(u0, numDescriptors = 1)), " \

RWTexture2D<float4> target : register (u0);
cbuffer CB0 : register(b0)
{
    float4 result;
    float4 padding[15];
}

[RootSignature(RootSig)]
[numthreads(32, 32, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    target[dtid.xy] = result;// float4(0.1f, 0.2f, 0.3f, 0.4f);
}