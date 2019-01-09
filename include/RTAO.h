// RTAO, Jakub Boksansky 2018
#pragma once

#include "Graphics.h"
#include "Profiler.h"
#include "Gui.h"

#include <chrono>

struct LowPassFilerCB
{
	XMMATRIX normalMatrix;
	XMFLOAT2 texelSize;
	int outputMode;

	LowPassFilerCB() {
		outputMode = 0;
	}
}; 

struct RtaoCB
{
	float aoRadius;
	int frameNumber;
	int samplesCount;
	int sampleStartIndex;

	RtaoCB() {
		aoRadius = 1.0f;
		samplesCount = 4;
		frameNumber = 0;
		sampleStartIndex = 0;
	}
};

class RTAO {
public:

	void Init(D3D12Global &d3d, D3D12Resources &resources, D3D12ShaderCompilerInfo &shaderCompiler, DXRGlobal &dxr, const Model &model);

	void Update(D3D12Global &d3d, D3D12Resources &resources);

	void Render(D3D12Global &d3d, D3D12Resources &resources, Profiler* profiler, Gui* gui);

	void Destroy(D3D12Resources &resources);

private:

	// DX12 resources initialization
	void createConstantBuffers(D3D12Global &d3d);
	void createRTAOBuffers(D3D12Global &d3d);

	void createRTAORayGenProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler);
	void createRTAOMissProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler);
	void createRTAOClosestHitProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler);

	void createRTAOPipelineStateObject(D3D12Global &d3d);
	void createRTAOCBVSRVUAVHeap(D3D12Global &d3d, DXRGlobal &dxr, D3D12Resources &resources, const Model &model);
	void createRTAOShaderTable(D3D12Global &d3d);

	void createFilterPipelineStateObject(D3D12Global &d3d);
	void createFilterRootSignature(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler);

	void initializeScreenQuadGeometry(D3D12Global &d3d);
	void createScreenQuadVertexBuffer(D3D12Global &d3d, Model &model);
	void createScreenQuadIndexBuffer(D3D12Global &d3d, Model &model);

	void createFilterCBVSRVUAVHeap(D3D12Global &d3d, D3D12Resources &resources);
	void createFilterDescriptorHeaps(D3D12Global &d3d);

	// DX12 helpers

	D3D12_CPU_DESCRIPTOR_HANDLE getBackBufferView(UINT currentBufferIndex, D3D12Resources &resources);
	D3D12_CPU_DESCRIPTOR_HANDLE getTempBufferView();

	UINT rtvDescSize;
	UINT cbvSrvUavDescSize;

	// Resources for low pass filter

	ID3D12Resource* screenQuadVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW screenQuadVertexBufferView;
	ID3D12Resource* screenQuadIndexBuffer;
	D3D12_INDEX_BUFFER_VIEW screenQuadIndexBufferView;

	RtProgram lowPassFilterProgramVS;
	RtProgram lowPassFilterXPassProgramPS;
	RtProgram lowPassFilterYPassProgramPS;
	
	ID3D12PipelineState* lowPassFilterXPassPso;
	ID3D12PipelineState* lowPassFilterYPassPso;
	ID3D12StateObjectProperties* lowPassFilterPsoInfo;
	
	ID3D12RootSignature*	lowPassFilterRootSignature;
	ID3D12DescriptorHeap*	lowPassFilterCbvSrvUavHeap;
	ID3D12DescriptorHeap*	lowPassFilterRtvHeap;
	
	LowPassFilerCB lowPassFilerInfo;
	ID3D12Resource* lowPassFilerInfoCB;
	UINT8* lowPassFilerInfoCBStart;

	ID3D12Resource* tempScreenBuffer;

	// Resources for AO raytracing
	RtProgram rayGenShader;
	RtProgram missShader;
	HitProgram hitShader;

	ID3D12StateObject* rtaoPipelineStateObject;
	ID3D12StateObjectProperties* rtaoPipelineStateObjectInfo;

	ID3D12Resource* sbtOdd;
	ID3D12Resource* sbtEven;
	uint32_t sbtEntrySize;

	ID3D12Resource* AOOutputEven;
	ID3D12Resource* AOOutputOdd;

	ID3D12Resource* AOSamples;
	ID3D12Resource* AOSamplesUploadHeap;

	ID3D12Resource* rtaoCB;
	RtaoCB rtaoCBData;
	UINT8* rtaoCBStart;

	ID3D12DescriptorHeap*	rtaoCbvSrvUavHeap;

	// Perf. measurements helpers
	std::chrono::steady_clock::time_point lastFPSUpdateTime;

	double lastAoFilteringTime;
	double lastAoRaytricingTime;
	double lastPrimaryRaysTime;
	
	// Window info
	D3D12_VIEWPORT screenViewport;
	D3D12_RECT scissorRect;
};