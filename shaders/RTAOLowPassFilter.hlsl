// RTAO - Low-pass filtering shader, Jakub Boksansky 2018

// ========================================================================
// Constant buffer inputs
// ========================================================================

struct LowPassFilerInfo
{
    float4x4 normalMatrix;
	float2 texelSize;
    int outputMode;
};

ConstantBuffer<LowPassFilerInfo> filterInfo : register(b0);

// ========================================================================
// Textures and Samplers
// ========================================================================

Texture2D depthNormalsTexture : register(t0);
Texture2D aoTexture : register(t1);
Texture2D colorTexture : register(t2);

SamplerState linearClampSampler : register(s0);

// ========================================================================
// Structures definitions 
// ========================================================================

struct ScreenQuadVertexInput
{
	float3 position : POSITION;
	float2 texCoords : TEXCOORDS;
};

struct ScreenQuadVertexOutput {
	float4 position : SV_POSITION;
	float2 texCoords : TEXCOORD0;
};

// ========================================================================
// Vertex shaders 
// ========================================================================

ScreenQuadVertexOutput lowPassFilterVS(ScreenQuadVertexInput vertexIn)
{
	ScreenQuadVertexOutput vertexOut;

	vertexOut.position = float4(vertexIn.position, 1.0f);
	vertexOut.texCoords = vertexIn.texCoords;

	return vertexOut;
}

// ========================================================================
// Pixel Shaders 
// ========================================================================

static const float gauss[5] =
{
	0.05448868f, 0.2442013f, 0.40262f, 0.2442013f, 0.05448868f
};

inline bool isValidTap(float tapDepth, float centerDepth, float3 tapNormal, float3 centerNormal, float dotViewNormal)
{

	const float depthRelativeDifferenceEpsilonMin = 0.003f;
	const float depthRelativeDifferenceEpsilonMax = 0.02f;
	const float dotNormalsEpsilon = 0.9f;

	// Adjust depth difference epsilon based on view space normal
    float depthRelativeDifferenceEpsilon = lerp(depthRelativeDifferenceEpsilonMax, depthRelativeDifferenceEpsilonMin, dotViewNormal);

	// Check depth
	if (abs(1.0f - (tapDepth / centerDepth)) > depthRelativeDifferenceEpsilon) return false;

	// Check normals
	if (dot(tapNormal, centerNormal) < dotNormalsEpsilon) return false;

	return true;
}

// Filter ambient occlusion using low-pass tent filter and mix it with color buffer
float lowPassFilter(ScreenQuadVertexOutput pixelInput, float2 filterDirection, const bool doTemporalFilter)
{
    float ao = 0.0f;
	float weight = 1.0f;

	float4 centerDepthNormal = depthNormalsTexture.Sample(linearClampSampler, pixelInput.texCoords);
	float centerDepth = centerDepthNormal.a;
	float3 centerNormal = normalize(centerDepthNormal.rgb);
    float dotViewNormal = abs(mul(filterInfo.normalMatrix, float4(centerNormal, 0.0f)).z);

    float2 offsetScale = filterInfo.texelSize * filterDirection;

	[unroll(5)]
    for (int i = -2; i <= 2; ++i)
    {
        float2 offset = float(i) * offsetScale;

		float4 tapDepthNormal = depthNormalsTexture.Sample(linearClampSampler, pixelInput.texCoords + offset);
		float4 tapAO = aoTexture.Sample(linearClampSampler, pixelInput.texCoords + offset);
		float tapDepth = tapDepthNormal.a;
		float3 tapNormal = normalize(tapDepthNormal.rgb);

		float tapWeight = gauss[i + 2];

        if (isValidTap(tapDepth, centerDepth, tapNormal, centerNormal, dotViewNormal))
            ao += (doTemporalFilter
                     ? dot(tapAO, float4(0.25f, 0.25f, 0.25f, 0.25f))
                     : tapAO.r) * tapWeight;
		else
			weight -= tapWeight;
    }
	
	ao /= weight;

	return ao;
}

float4 lowPassFilterXPassPS(ScreenQuadVertexOutput pixelInput) : SV_Target
{
    return lowPassFilter(pixelInput, float2(1.0f, 0.0f), true).xxxx;
}

float4 lowPassFilterYPassPS(ScreenQuadVertexOutput pixelInput) : SV_Target
{
    float ao = lowPassFilter(pixelInput, float2(0.0f, 1.0f), false);

	float4 color = colorTexture.Sample(linearClampSampler, pixelInput.texCoords);

    if (filterInfo.outputMode == 0) return float4(ao, ao, ao, ao);
    if (filterInfo.outputMode == 2) return color;

	return float4(color.rgb * ao, 1.0f);
}