RWTexture2D<float4> target : register (u0);

[numthreads(32, 32, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	target[dtid.xy] = float4(0.1f, 0.2f, 0.3f, 0.4f);
}