// ---[ Structures ]---

struct HitInfo
{
	float T;
};

struct Attributes {
	float2 uv;
};

// ---[ Constant Buffers ]---

cbuffer ViewCB : register(b0)
{
	matrix view;
    matrix motionVectorsMatrix;
	matrix previousViewMatrix;
	float4 viewOriginAndTanHalfFovY;
	float2 resolution;
};

cbuffer RtaoCB : register(b1) 
{
    float aoRadius;
    int frameNumber;
	int samplesCount;
    int sampleStartIndex;
};

// ---[ Resources ]---

RWTexture2D<float4> aoOutput				: register(u0);

RaytracingAccelerationStructure SceneBVH	: register(t0);
Texture2D<float4> aoPrevious				: register(t1);
Texture2D<float4> normalAndDepthsCurrent    : register(t2);
Texture2D<float4> normalAndDepthsPrevious   : register(t3);
Texture1D<float3> aoSamplesTexture          : register(t4);
