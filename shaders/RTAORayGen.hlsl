// RTAO - ray generation shader, Jakub Boksansky 2018
#include "RTAOCommon.hlsl"

SamplerState linearClampSampler : register(s0);

[shader("raygeneration")]
void RayGen()
{
	uint2 LaunchIndex = DispatchRaysIndex().xy;
	uint2 LaunchDimensions = DispatchRaysDimensions().xy;

	float2 d = (((LaunchIndex.xy + 0.5f) / resolution.xy) * 2.f - 1.f);
	float aspectRatio = (resolution.x / resolution.y);
    
	// Figure out pixel world space position (using length of a primary ray found in previous pass)
    float3 primaryRayOrigin = viewOriginAndTanHalfFovY.xyz;
	float3 primaryRayDirection = normalize((d.x * view[0].xyz * viewOriginAndTanHalfFovY.w * aspectRatio) - (d.y * view[1].xyz * viewOriginAndTanHalfFovY.w) + view[2].xyz);
	float4 normalAndDepth = normalAndDepthsCurrent.Load(int3(LaunchIndex, 0));
	float3 pixelWorldSpacePosition = primaryRayOrigin + (primaryRayDirection * normalAndDepth.w);

	if (normalAndDepth.w < 0.0f)
	{
        // Terminate if primary ray didn't hit anything
		aoOutput[LaunchIndex] = float4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	// Calculate AO

	// Pick subset of samples to use based on "frame number % 4" and position on screen within a block of 3x3 pixels
    int pixelIdx = dot(int2(fmod(float2(LaunchIndex), 3)), int2(1, 3));
    int currentSamplesStartIndex = sampleStartIndex + (pixelIdx * samplesCount) + int(frameNumber * samplesCount * 9);

	// Construct TBN matrix to orient sampling hemisphere along the surface normal
	float3 n = normalize(normalAndDepth.xyz);
   
    float3 rvec = primaryRayDirection;
	float3 b1 = normalize(rvec - n * dot(rvec, n));
	float3 b2 = cross(n, b1);
	float3x3 tbn = float3x3(b1, b2, n);

	RayDesc aoRay;
	HitInfo aoHitData;

	aoRay.Origin = pixelWorldSpacePosition;
	aoRay.TMin = 0.1f;
    aoRay.TMax = aoRadius; //< Set max ray length to AO radius for early termination

	float ao = 0.0f;

	[unroll(4)]
	for (int i = 0; i < samplesCount; i++)
	{
		float3 aoSampleDirection = mul(aoSamplesTexture.Load(int2(currentSamplesStartIndex + i, 0)).rgb, tbn);

		// Setup the ray
		aoRay.Direction = aoSampleDirection;
        aoHitData.T = aoRadius; //< Set T to "maximum", to produce no occlusion in case ray doesn't hit anything (miss shader won't modify this value)

		// Trace the ray
		TraceRay(
			SceneBVH,
			RAY_FLAG_NONE,
			0xFF,
			0,
			0,
			0,
			aoRay,
			aoHitData);

		ao += aoHitData.T / aoRadius;
	}

	ao /= float(samplesCount);

	// Reverse reprojection
	float4 previousPixelViewSpacePosition = mul(previousViewMatrix, float4(pixelWorldSpacePosition, 1.0f));
	float previousReprojectedLinearDepth = length(previousPixelViewSpacePosition.xyz);
	float4 previousPixelScreenSpacePosition = mul(motionVectorsMatrix, float4(pixelWorldSpacePosition, 1.0f));
	previousPixelScreenSpacePosition.xyz /= previousPixelScreenSpacePosition.w;

	float2 previousUvs = (previousPixelScreenSpacePosition.xy * float2(0.5f, -0.5f)) + 0.5f;

	bool isReprojectionValid = true;

	// Discard invalid reprojection (outside of the frame)
	if (previousUvs.x > 1.0f || previousUvs.x < 0.0f ||
        previousUvs.y > 1.0f || previousUvs.y < 0.0f)
		isReprojectionValid = false;

	// Discard invalid reprojection (depth mismatch)
	const float maxReprojectionDepthDifference = 0.03f;
	float previousSampledLinearDepth = normalAndDepthsPrevious.SampleLevel(linearClampSampler, previousUvs.xy, 0).a;

	if (abs(1.0f - (previousReprojectedLinearDepth / previousSampledLinearDepth)) > maxReprojectionDepthDifference)
		isReprojectionValid = false;

    // Store AO to history cache
	if (isReprojectionValid) 
        aoOutput[LaunchIndex.xy] = float4(ao, aoPrevious.SampleLevel(linearClampSampler, previousUvs.xy, 0).rgb); //< Store current AO in history cache
    else
        aoOutput[LaunchIndex.xy] = float4(ao, ao, ao, ao); //< Replace all cached AO with current result

}
