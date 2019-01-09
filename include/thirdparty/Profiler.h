// This is an over-simplified version of profiler found in MJP's DX12 Sample Framework (released under the MIT license) at https://github.com/TheRealMJP/DeferredTexturing. 
// For a much better profiler and many more cool DX12 stuff, you should visit his pages at http://mynameismjp.wordpress.com/ 

#pragma once

#include "Graphics.h"

struct ProfileData
{
	const char* Name = nullptr;

	bool QueryStarted = false;
	bool QueryFinished = false;
	bool Active = false;

	bool CPUProfile = false;
	int64_t StartTime = 0;
	int64_t EndTime = 0;

	static const int64_t FilterSize = 64;
	double TimeSamples[FilterSize] = {};
	int64_t CurrSample = 0;
};

struct ReadbackBuffer
{
	ID3D12Resource* Resource = nullptr;
	uint64_t Size = 0;

	ReadbackBuffer();
	~ReadbackBuffer();

	void Init(D3D12Global &d3d, uint64_t size);
	void Destroy();

	void* Map();
	template<typename T> T* Map() { return reinterpret_cast<T*>(Map()); };
	void Unmap();

private:

	ReadbackBuffer(const ReadbackBuffer& other) { }
};


class Profiler
{
public:

	void Init(D3D12Global &d3d);
	void Destroy();

	uint64_t StartProfile(ID3D12GraphicsCommandList* cmdList, const char* name);
	double EndProfile(D3D12Global &d3d, ID3D12GraphicsCommandList* cmdList, uint64_t idx);

protected:

	ID3D12QueryHeap* queryHeap = nullptr;
	uint64_t numProfiles = 0;

	std::vector<ProfileData> profiles;

	ReadbackBuffer readbackBuffer;
};
