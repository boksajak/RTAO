// This is an over-simplified version of profiler found in MJP's DX12 Sample Framework (released under the MIT license) at https://github.com/TheRealMJP/DeferredTexturing. 
// For a much better profiler and many more cool DX12 stuff, you should visit his pages at http://mynameismjp.wordpress.com/ 

#include "Profiler.h"

// == ReadbackBuffer ==============================================================================

ReadbackBuffer::ReadbackBuffer()
{
}

ReadbackBuffer::~ReadbackBuffer()
{
}

void ReadbackBuffer::Init(D3D12Global &d3d, uint64_t size)
{
	Size = size;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = uint32_t(size);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Alignment = 0;

	D3D12_HEAP_PROPERTIES heapProps =
	{
		D3D12_HEAP_TYPE_READBACK,
		D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		D3D12_MEMORY_POOL_UNKNOWN,
		0,
		0,
	};

	d3d.device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Resource));
}

void ReadbackBuffer::Destroy()
{
	SAFE_RELEASE(Resource);
	Size = 0;
}

void* ReadbackBuffer::Map()
{
	void* data = nullptr;
	D3D12_RANGE range;
	range.Begin = SIZE_T(0);
	range.End = SIZE_T(Size);

	Resource->Map(0, &range, &data);
	return data;
}

void ReadbackBuffer::Unmap()
{
	Resource->Unmap(0, nullptr);
}


// == Profiler ====================================================================================

static const uint64_t MaxProfiles = 64;

void Profiler::Init(D3D12Global &d3d)
{
	Destroy();

	D3D12_QUERY_HEAP_DESC heapDesc = {};
	heapDesc.Count = MaxProfiles * 2;
	heapDesc.NodeMask = 0;
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	d3d.device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&queryHeap));

	uint64_t RenderLatency = 2;
	readbackBuffer.Init(d3d, MaxProfiles * RenderLatency * 2 * sizeof(int64_t));

	profiles.resize(MaxProfiles);
}

void Profiler::Destroy()
{
	SAFE_RELEASE(queryHeap);
	readbackBuffer.Destroy();
	numProfiles = 0;
}

uint64_t Profiler::StartProfile(ID3D12GraphicsCommandList* cmdList, const char* name)
{

	uint64_t profileIdx = uint64_t(-1);
	for (uint64_t i = 0; i < numProfiles; ++i)
	{
		if (profiles[i].Name == name)
		{
			profileIdx = i;
			break;
		}
	}

	if (profileIdx == int64_t(-1))
	{
		profileIdx = numProfiles++;
		profiles[profileIdx].Name = name;
	}

	ProfileData& profileData = profiles[profileIdx];
	profileData.CPUProfile = false;
	profileData.Active = true;

	// Insert the start timestamp
	const uint32_t startQueryIdx = uint32_t(profileIdx * 2);
	cmdList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, startQueryIdx);

	profileData.QueryStarted = true;

	return profileIdx;
}

double Profiler::EndProfile(D3D12Global &d3d, ID3D12GraphicsCommandList* cmdList, uint64_t idx)
{

	ProfileData& profileData = profiles[idx];

	// Insert the end timestamp
	const uint32_t startQueryIdx = uint32_t(idx * 2);
	const uint32_t endQueryIdx = startQueryIdx + 1;
	cmdList->EndQuery(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, endQueryIdx);

	// Resolve the data
	const uint64_t dstOffset = ((d3d.frameIndex * MaxProfiles * 2) + startQueryIdx) * sizeof(uint64_t);
	cmdList->ResolveQueryData(queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, startQueryIdx, 2, readbackBuffer.Resource, dstOffset);

	profileData.QueryStarted = false;
	profileData.QueryFinished = true;

	uint64_t gpuFrequency = 0;
	const uint64_t* frameQueryData = nullptr;
	d3d.cmdQueue->GetTimestampFrequency(&gpuFrequency);

	const uint64_t* queryData = readbackBuffer.Map<uint64_t>();
	frameQueryData = queryData + (d3d.frameIndex * MaxProfiles * 2);

	// Get the query data
	uint64_t startTime = frameQueryData[idx * 2 + 0];
	uint64_t endTime = frameQueryData[idx * 2 + 1];

	double time = 0.0f;

	if (endTime > startTime)
	{
		uint64_t delta = endTime - startTime;
		double frequency = double(gpuFrequency);
		time = (delta / frequency) * 1000.0;
	}

	readbackBuffer.Unmap();

	return time;
}

