// RTAO, Jakub Boksansky 2018
#include "RTAO.h"
#include "Graphics.h"

#include "Samples.h"

void RTAO::Init(D3D12Global &d3d, D3D12Resources &resources, D3D12ShaderCompilerInfo &shaderCompiler, DXRGlobal &dxr, const Model &model) {
	
	// Initialize gui update time
	lastFPSUpdateTime = std::chrono::steady_clock::now();

	// Create DX12 resources
	createConstantBuffers(d3d);
	createRTAOBuffers(d3d);

	createRTAORayGenProgram(d3d, shaderCompiler);
	createRTAOMissProgram(d3d, shaderCompiler);
	createRTAOClosestHitProgram(d3d, shaderCompiler);

	createRTAOCBVSRVUAVHeap(d3d, dxr, resources, model);
	createRTAOPipelineStateObject(d3d);
	createRTAOShaderTable(d3d);

	createFilterCBVSRVUAVHeap(d3d, resources);
	createFilterDescriptorHeaps(d3d);
	createFilterRootSignature(d3d, shaderCompiler);
	createFilterPipelineStateObject(d3d);

	initializeScreenQuadGeometry(d3d);

	// Update the viewport transform to cover the client area.
	screenViewport.TopLeftX = 0;
	screenViewport.TopLeftY = 0;
	screenViewport.Width = static_cast<float>(d3d.width);
	screenViewport.Height = static_cast<float>(d3d.height);
	screenViewport.MinDepth = 0.0f;
	screenViewport.MaxDepth = 1.0f;

	scissorRect = { 0, 0, (LONG)d3d.width, (LONG)d3d.height };

}

void RTAO::Update(D3D12Global &d3d, D3D12Resources &resources) {

	// Update RTAO CB
	rtaoCBData.frameNumber = d3d.frameNumber;

	switch (rtaoCBData.samplesCount) {
	case 1: rtaoCBData.sampleStartIndex = 0; break;
	case 2: rtaoCBData.sampleStartIndex = 36; break;
	case 3: rtaoCBData.sampleStartIndex = 36 + 72; break;
	case 4: rtaoCBData.sampleStartIndex = 36 + 72 + 108; break;
	}

	memcpy(rtaoCBStart, &rtaoCBData, sizeof(rtaoCBData));

	// Update low pass filter CB
	lowPassFilerInfo.normalMatrix = resources.normalMatrix;
	lowPassFilerInfo.texelSize = XMFLOAT2(1.0f / d3d.width, 1.0f / d3d.height);

	memcpy(lowPassFilerInfoCBStart, &lowPassFilerInfo, sizeof(LowPassFilerCB));
}

void RTAO::Render(D3D12Global &d3d, D3D12Resources &resources, Profiler* profiler, Gui* gui) {

	bool isEvenFrame = (d3d.frameNumber % 2) == 0;

	// 1. AO Raytracing Step ====================================================================

	// Start profiling
	uint64_t aoRaytracingProfileId = profiler->StartProfile(d3d.cmdList, "AO_RAYS");

	// Set raytracing step descriptor heap
	d3d.cmdList->SetDescriptorHeaps(1, &rtaoCbvSrvUavHeap);

	// Dispatch rays
	ID3D12Resource* sbt = isEvenFrame ? sbtEven : sbtOdd;

	D3D12_DISPATCH_RAYS_DESC desc = {};
	desc.RayGenerationShaderRecord.StartAddress = sbt->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = sbtEntrySize;

	desc.MissShaderTable.StartAddress = sbt->GetGPUVirtualAddress() + sbtEntrySize;
	desc.MissShaderTable.SizeInBytes = sbtEntrySize;		// Only a single Miss program entry
	desc.MissShaderTable.StrideInBytes = sbtEntrySize;

	desc.HitGroupTable.StartAddress = sbt->GetGPUVirtualAddress() + (sbtEntrySize * 2);
	desc.HitGroupTable.SizeInBytes = sbtEntrySize;			// Only a single Hit program entry
	desc.HitGroupTable.StrideInBytes = sbtEntrySize;

	desc.Width = d3d.width;
	desc.Height = d3d.height;
	desc.Depth = 1;

	d3d.cmdList->SetPipelineState1(rtaoPipelineStateObject);
	d3d.cmdList->DispatchRays(&desc);

	// 2. Filtering Step ========================================================================

	// Stop previous and start new profiling
	double aoRaytracingTime = profiler->EndProfile(d3d, d3d.cmdList, aoRaytracingProfileId);
	uint64_t aoFilteringProfileId = profiler->StartProfile(d3d.cmdList, "AO_FILTERING");

	// Set filtering step descriptor heap
	d3d.cmdList->SetDescriptorHeaps(1, &lowPassFilterCbvSrvUavHeap);

	// Set viewport
	d3d.cmdList->RSSetViewports(1, &screenViewport);
	d3d.cmdList->RSSetScissorRects(1, &scissorRect);

	// Transition resources
	D3D12_RESOURCE_BARRIER Barriers[7] = {};

	Barriers[0].Transition.pResource = d3d.backBuffer[d3d.frameIndex];
	Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	Barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	Barriers[1].Transition.pResource = resources.DXROutput;
	Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	Barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	if (isEvenFrame) {

		Barriers[2].Transition.pResource = AOOutputOdd;
		Barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[3].Transition.pResource = AOOutputEven;
		Barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[4].Transition.pResource = resources.depthNormalsOutputOdd;
		Barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[4].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[5].Transition.pResource = resources.depthNormalsOutputEven;
		Barriers[5].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[5].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[5].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	}
	else {

		Barriers[2].Transition.pResource = AOOutputEven;
		Barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[3].Transition.pResource = AOOutputOdd;
		Barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[4].Transition.pResource = resources.depthNormalsOutputEven;
		Barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[4].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		Barriers[5].Transition.pResource = resources.depthNormalsOutputOdd;
		Barriers[5].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		Barriers[5].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		Barriers[5].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	}

	Barriers[6].Transition.pResource = tempScreenBuffer;
	Barriers[6].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Barriers[6].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barriers[6].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	// Wait for the transitions to complete
	d3d.cmdList->ResourceBarrier(_countof(Barriers), Barriers);

	// Set heap pointer based on frame number parity (even/odd)
	D3D12_GPU_DESCRIPTOR_HANDLE heapPtr = lowPassFilterCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	if (isEvenFrame) heapPtr.ptr += 4 * cbvSrvUavDescSize;

	// Change pipeline state for filtering pass
	d3d.cmdList->SetGraphicsRootSignature(lowPassFilterRootSignature);
	d3d.cmdList->SetGraphicsRootDescriptorTable(0, heapPtr);
	d3d.cmdList->SetPipelineState(lowPassFilterXPassPso);

	// Set vertex and index buffer (screenquad)
	d3d.cmdList->IASetVertexBuffers(0, 1, &screenQuadVertexBufferView);
	d3d.cmdList->IASetIndexBuffer(&screenQuadIndexBufferView);

	// Set mesh topology
	d3d.cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set filter to horizontal direction (first pass)
	d3d.cmdList->OMSetRenderTargets(1, &getTempBufferView(), true, nullptr);

	// Issue draw command
	d3d.cmdList->DrawIndexedInstanced(6 /* we have 6 indices */, 1 /* and 1 instance */, 0, 0, 0);

	d3d.cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(tempScreenBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	// Set filter to vertical direction

	// Move heap pointer to second pass
	heapPtr.ptr += 8 * cbvSrvUavDescSize;

	d3d.cmdList->SetGraphicsRootDescriptorTable(0, heapPtr);
	d3d.cmdList->SetPipelineState(lowPassFilterYPassPso);

	D3D12_CPU_DESCRIPTOR_HANDLE destination = getBackBufferView(d3d.frameIndex, resources);

	// Specify the buffers we are going to render to - destination (back buffer)
	d3d.cmdList->OMSetRenderTargets(1, &destination, true, nullptr);

	// Issue draw command
	d3d.cmdList->DrawIndexedInstanced(6 /* we have 6 indices */, 1 /* and 1 instance */, 0, 0, 0);

	// Indicate that the back buffer will now be used to present.
	Barriers[0].Transition.pResource = d3d.backBuffer[d3d.frameIndex];
	Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	Barriers[1].Transition.pResource = resources.DXROutput;
	Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	Barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	d3d.cmdList->ResourceBarrier(2, Barriers);

	// End profiling and draw GUI ====================================================================

	double aoFilteringTime = profiler->EndProfile(d3d, d3d.cmdList, aoFilteringProfileId);

	if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastFPSUpdateTime).count() > 100) {
		lastFPSUpdateTime = std::chrono::steady_clock::now();
		lastAoFilteringTime = aoFilteringTime;
		lastAoRaytricingTime = aoRaytracingTime;
		lastPrimaryRaysTime = d3d.primaryRaysTime;
	};

	gui->Text("Primary Rays Time: %.02fms", lastPrimaryRaysTime);
	gui->Text("AO Raytracing Time: %.02fms", lastAoRaytricingTime);
	gui->Text("AO Filtering Time: %.02fms", lastAoFilteringTime);
	gui->Text("Total Frame Time: %.02fms", lastPrimaryRaysTime + lastAoRaytricingTime + lastAoFilteringTime);

	gui->SliderFloat("AO Radius", &rtaoCBData.aoRadius, 0.01f, 2.0f);
	gui->SliderInt("AO Rays Count", &rtaoCBData.samplesCount, 1, 4);
	gui->Combo("Output Mode", &lowPassFilerInfo.outputMode, "AO Only\0AO & Color\0Color Only");

	// Submit the command list and wait for the GPU to idle ===========================================
	D3D12::Submit_CmdList(d3d);
	D3D12::WaitForGPU(d3d);
}

void RTAO::Destroy(D3D12Resources &resources) {

	SAFE_RELEASE(sbtOdd);
	SAFE_RELEASE(sbtEven);

	SAFE_RELEASE(rayGenShader.blob);
	SAFE_RELEASE(missShader.blob);
	SAFE_RELEASE(hitShader.chs.blob);

	SAFE_RELEASE(rtaoPipelineStateObject);

	if (rtaoCB) rtaoCB->Unmap(0, nullptr);
	if (rtaoCBStart) rtaoCBStart = nullptr;
	if (lowPassFilerInfoCB) lowPassFilerInfoCB->Unmap(0, nullptr);
	if (lowPassFilerInfoCBStart) lowPassFilerInfoCBStart = nullptr;

	SAFE_RELEASE(tempScreenBuffer);
	SAFE_RELEASE(AOOutputEven);
	SAFE_RELEASE(AOOutputOdd);
	SAFE_RELEASE(AOSamples);
	SAFE_RELEASE(AOSamplesUploadHeap);

	SAFE_RELEASE(screenQuadIndexBuffer);
	SAFE_RELEASE(screenQuadVertexBuffer);

	SAFE_RELEASE(rtaoCbvSrvUavHeap);
	SAFE_RELEASE(lowPassFilterCbvSrvUavHeap);
	SAFE_RELEASE(lowPassFilterRtvHeap);

	SAFE_RELEASE(lowPassFilterRootSignature);
	SAFE_RELEASE(lowPassFilterXPassPso);
	SAFE_RELEASE(lowPassFilterYPassPso);
	
	SAFE_RELEASE(lowPassFilterProgramVS.blob);
	SAFE_RELEASE(lowPassFilterXPassProgramPS.blob);
	SAFE_RELEASE(lowPassFilterYPassProgramPS.blob);

}

void RTAO::createConstantBuffers(D3D12Global &d3d) {

	// Create and initialize constant buffer for AO raytracing.
	D3DResources::Create_Constant_Buffer(d3d, &rtaoCB, sizeof(RtaoCB));

	HRESULT hr = rtaoCB->Map(0, nullptr, reinterpret_cast<void**>(&rtaoCBStart));
	Utils::Validate(hr, L"Error: failed to map RTAO constant buffer!");

	memcpy(rtaoCBStart, &rtaoCBData, sizeof(rtaoCBData));

	// Create and initialize the filter constant buffer.
	D3DResources::Create_Constant_Buffer(d3d, &lowPassFilerInfoCB, sizeof(LowPassFilerCB));

	hr = lowPassFilerInfoCB->Map(0, nullptr, reinterpret_cast<void**>(&lowPassFilerInfoCBStart));
	Utils::Validate(hr, L"Error: failed to map low pass filter constant buffer!");

	memcpy(lowPassFilerInfoCBStart, &lowPassFilerInfo, sizeof(LowPassFilerCB));

}

void RTAO::createScreenQuadVertexBuffer(D3D12Global &d3d, Model &model)
{
	// Create the buffer resource from the model's vertices
	D3D12BufferCreateInfo info(((UINT)model.vertices.size() * sizeof(Vertex)), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	D3DResources::Create_Buffer(d3d, info, &screenQuadVertexBuffer);

#if defined(_DEBUG)
	screenQuadVertexBuffer->SetName(L"ScreenQuadVertexBuffer");
#endif

	// Copy the vertex data to the vertex buffer
	UINT8* pVertexDataBegin;
	D3D12_RANGE readRange = {};
	HRESULT hr = screenQuadVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	Utils::Validate(hr, L"Error: failed to map vertex buffer!");

	memcpy(pVertexDataBegin, model.vertices.data(), info.size);
	screenQuadVertexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view
	screenQuadVertexBufferView.BufferLocation = screenQuadVertexBuffer->GetGPUVirtualAddress();
	screenQuadVertexBufferView.StrideInBytes = sizeof(Vertex);
	screenQuadVertexBufferView.SizeInBytes = static_cast<UINT>(info.size);
}

void RTAO::createScreenQuadIndexBuffer(D3D12Global &d3d, Model &model)
{
	// Create the index buffer resource
	D3D12BufferCreateInfo info((UINT)model.indices.size() * sizeof(UINT), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	D3DResources::Create_Buffer(d3d, info, &screenQuadIndexBuffer);

#if defined(_DEBUG)
	screenQuadIndexBuffer->SetName(L"ScreenQuadIndexBuffer");
#endif

	// Copy the index data to the index buffer
	UINT8* pIndexDataBegin;
	D3D12_RANGE readRange = {};
	HRESULT hr = screenQuadIndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin));
	Utils::Validate(hr, L"Error: failed to map index buffer!");

	memcpy(pIndexDataBegin, model.indices.data(), info.size);
	screenQuadIndexBuffer->Unmap(0, nullptr);

	// Initialize the index buffer view
	screenQuadIndexBufferView.BufferLocation = screenQuadIndexBuffer->GetGPUVirtualAddress();
	screenQuadIndexBufferView.SizeInBytes = static_cast<UINT>(info.size);
	screenQuadIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

void RTAO::initializeScreenQuadGeometry(D3D12Global &d3d) {

	Model screenQuad;

	screenQuad.vertices = {
		Vertex{
			XMFLOAT3(-1.0f, -1.0f, 0.0f), //< Position
			XMFLOAT2(0.0f, 1.0f), //< Texture coords
		},
		Vertex{
			XMFLOAT3(1.0f, -1.0f, 0.0f), //< Position
			XMFLOAT2(1.0f, 1.0f), //< Texture coords
		},
		Vertex{
			XMFLOAT3(1.0f, 1.0f, 0.0f), //< Position
			XMFLOAT2(1.0f, 0.0f), //< Texture coords
		},
		Vertex{
			XMFLOAT3(-1.0f, 1.0f, 0.0f), //< Position
			XMFLOAT2(0.0f, 0.0f), //< Texture coords
		},
	};

	screenQuad.indices = {
		0, 2, 1,
		0, 3, 2
	};

	createScreenQuadVertexBuffer(d3d, screenQuad);
	createScreenQuadIndexBuffer(d3d, screenQuad);
}

/**
* Create the DXR shader table.
*/
void RTAO::createRTAOShaderTable(D3D12Global &d3d)
{
	/*
	The Shader Table layout is as follows:
	Entry 0 - Ray Generation program
	Entry 1 - Miss program
	Entry 2 - Closest Hit program
	All entries in the SBT must have the same size, so we will choose it base on the largest required entry.
	The ray-gen program requires the largest entry - sizeof(program identifier) + 4 bytes for a descriptor-table + 8 bytes for a constant buffer descriptor.
	The entry size must be aligned up to D3D12_RAYTRACING_SHADER_BINDING_TABLE_RECORD_BYTE_ALIGNMENT
	*/

	uint32_t progIdSize = 32;
	uint32_t sbtSize = 0;

	sbtEntrySize = progIdSize;
	sbtEntrySize += 8;					// CBV/SRV/UAV descriptor table
	sbtEntrySize = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, sbtEntrySize);

	sbtSize = (sbtEntrySize * 3);
	sbtSize = ALIGN(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, sbtSize);

	// Create the shader table buffers
	D3D12BufferCreateInfo bufferInfo(sbtSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	D3DResources::Create_Buffer(d3d, bufferInfo, &sbtOdd);
	D3DResources::Create_Buffer(d3d, bufferInfo, &sbtEven);

	// Map the buffer
	uint8_t* pData;
	HRESULT hr = sbtOdd->Map(0, nullptr, (void**)&pData);
	Utils::Validate(hr, L"Error: failed to map shader binding table!");

	// Entry 0 - Ray Generation program and local root argument data (descriptor table)
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"RayGen_12"), progIdSize);

	// Set the root arguments data. Point to start of descriptor heap
	*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + progIdSize) = rtaoCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

	// Entry 1 - Miss program (no local root arguments to set)
	pData += sbtEntrySize;
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"Miss_5"), progIdSize);

	// Entry 2 - Closest Hit program and local root argument data (descriptor table, constant buffer, and IB/VB pointers)
	pData += sbtEntrySize;
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"HitGroup"), progIdSize);

	// Set the root arg data. Point to start of descriptor heap
	*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + progIdSize) = rtaoCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

	// Unmap
	sbtOdd->Unmap(0, nullptr);

	// Even frame
	hr = sbtEven->Map(0, nullptr, (void**)&pData);
	Utils::Validate(hr, L"Error: failed to map shader binding table!");

	// Entry 0 - Ray Generation program and local root argument data (descriptor table)
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"RayGen_12"), progIdSize);

	auto tempHandle = rtaoCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
	tempHandle.ptr += 8 * d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Set the root arguments data. Point to start of descriptor heap
	*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + progIdSize) = tempHandle;

	// Entry 1 - Miss program (no local root arguments to set)
	pData += sbtEntrySize;
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"Miss_5"), progIdSize);

	// Entry 2 - Closest Hit program and local root argument data (descriptor table, constant buffer, and IB/VB pointers)
	pData += sbtEntrySize;
	memcpy(pData, rtaoPipelineStateObjectInfo->GetShaderIdentifier(L"HitGroup"), progIdSize);

	// Set the root arg data. Point to start of descriptor heap
	*reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(pData + progIdSize) = tempHandle;

	// Unmap
	sbtEven->Unmap(0, nullptr);
}

void RTAO::createRTAOCBVSRVUAVHeap(D3D12Global &d3d, DXRGlobal &dxr, D3D12Resources &resources, const Model &model)
{
	// Describe the CBV/SRV/UAV heap
	// Need 16 entries:
	// 1 CBV for the ViewCB
	// 1 CBV for the RtaoCB

	// 1 UAV for the AO output (odd frame)

	// 1 SRV for the Scene BVH (odd frame)
	// 1 SRV for the AO source (odd frame)
	// 1 SRV for the depth & normals current (odd frame)
	// 1 SRV for the depth & normals previous (odd frame)
	// 1 SRV for the AO samples (odd frame)

	// 1 CBV for the ViewCB
	// 1 CBV for the RtaoCB

	// 1 UAV for the AO output (even frame)

	// 1 SRV for the Scene BVH (even frame)
	// 1 SRV for the AO source (even frame)
	// 1 SRV for the depth & normals current (even frame)
	// 1 SRV for the depth & normals previous (even frame)
	// 1 SRV for the AO samples (even frame)

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 16;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// Create the descriptor heap
	HRESULT hr = d3d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtaoCbvSrvUavHeap));
	Utils::Validate(hr, L"Error: failed to create DXR CBV/SRV/UAV descriptor heap!");

	// Get the descriptor heap handle and increment size
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtaoCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
	UINT handleIncrement = d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Odd frame -------------------------------------------------------------------------------------------

	// Create the ViewCB CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(resources.viewCBData));
	cbvDesc.BufferLocation = resources.viewCB->GetGPUVirtualAddress();

	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the RtaoCB CBV
	cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(rtaoCBData));
	cbvDesc.BufferLocation = rtaoCB->GetGPUVirtualAddress();

	handle.ptr += handleIncrement;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the DXR output buffer UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	// Create the AO output buffer UAV
	handle.ptr += handleIncrement;
	d3d.device->CreateUnorderedAccessView(AOOutputEven, nullptr, &uavDesc, handle);

	// Create the DXR Top Level Acceleration Structure SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = dxr.TLAS.pResult->GetGPUVirtualAddress();

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(nullptr, &srvDesc, handle);

	// Create the material texture SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Texture2D.MipLevels = 1;
	textureSRVDesc.Texture2D.MostDetailedMip = 0;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(AOOutputOdd, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputEven, &textureSRVDesc, handle);

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputOdd, &textureSRVDesc, handle);

	// Ao samples
	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(AOSamples, &textureSRVDesc, handle);

	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Even frame -------------------------------------------------------------------------------------------

	// Create the ViewCB CBV
	cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(resources.viewCBData));
	cbvDesc.BufferLocation = resources.viewCB->GetGPUVirtualAddress();

	handle.ptr += handleIncrement;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the RtaoCB CBV
	cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(rtaoCBData));
	cbvDesc.BufferLocation = rtaoCB->GetGPUVirtualAddress();

	handle.ptr += handleIncrement;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the AO output buffer UAV
	handle.ptr += handleIncrement;
	d3d.device->CreateUnorderedAccessView(AOOutputOdd, nullptr, &uavDesc, handle);

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(nullptr, &srvDesc, handle);

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(AOOutputEven, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputOdd, &textureSRVDesc, handle);

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputEven, &textureSRVDesc, handle);

	// Ao samples
	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;

	handle.ptr += handleIncrement;
	d3d.device->CreateShaderResourceView(AOSamples, &textureSRVDesc, handle);

}

void RTAO::createFilterCBVSRVUAVHeap(D3D12Global &d3d, D3D12Resources &resources)
{
	// Describe the CBV/SRV/UAV heap
	// Need 16 in total
	// Need 4 entries - first filter pass (even frame):
	// 1 CBV for the LowPassFilerInfo
	// 1 SRV for the depthNormalsTexture
	// 1 SRV for the aoTexture
	// 1 SRV for the colorTexture

	// Need 4 entries - first filter pass (odd frame):
	// 1 CBV for the LowPassFilerInfo
	// 1 SRV for the depthNormalsTexture
	// 1 SRV for the aoTexture
	// 1 SRV for the colorTexture

	// Need 4 entries - second filter pass (even frame):
	// 1 CBV for the LowPassFilerInfo
	// 1 SRV for the depthNormalsTexture
	// 1 SRV for the tempScreenTexture
	// 1 SRV for the colorTexture

	// Need 4 entries - second filter pass (odd frame):
	// 1 CBV for the LowPassFilerInfo
	// 1 SRV for the depthNormalsTexture
	// 1 SRV for the tempScreenTexture
	// 1 SRV for the colorTexture

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 16;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	// Create the descriptor heap
	HRESULT hr = d3d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&lowPassFilterCbvSrvUavHeap));
	Utils::Validate(hr, L"Error: failed to create DXR CBV/SRV/UAV descriptor heap!");

	// Get the descriptor heap handle and increment size
	D3D12_CPU_DESCRIPTOR_HANDLE handle = lowPassFilterCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
	cbvSrvUavDescSize = d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// First filter pass (horizontal), even frame

	// Create the LowPassFilerInfo CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.SizeInBytes = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(LowPassFilerCB));
	cbvDesc.BufferLocation = lowPassFilerInfoCB->GetGPUVirtualAddress();

	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the texture SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC textureSRVDesc = {};
	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSRVDesc.Texture2D.MipLevels = 1;
	textureSRVDesc.Texture2D.MostDetailedMip = 0;
	textureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputEven, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(AOOutputEven, &textureSRVDesc, handle);

	// Create the DXROutput SRV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.DXROutput, &textureSRVDesc, handle);

	// First filter pass (horizontal), odd frame

	// Create the LowPassFilerInfo CBV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputOdd, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(AOOutputOdd, &textureSRVDesc, handle);

	// Create the DXROutput SRV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.DXROutput, &textureSRVDesc, handle);

	// Second filter pass (vertical), even frame

	// Create the LowPassFilerInfo CBV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the texture SRV
	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputEven, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(tempScreenBuffer, &textureSRVDesc, handle);

	// Create the DXROutput SRV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.DXROutput, &textureSRVDesc, handle);

	// Second filter pass (vertical), odd frame

	// Create the LowPassFilerInfo CBV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateConstantBufferView(&cbvDesc, handle);

	// Create the texture SRV
	textureSRVDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.depthNormalsOutputOdd, &textureSRVDesc, handle);

	textureSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(tempScreenBuffer, &textureSRVDesc, handle);

	// Create the DXROutput SRV
	handle.ptr += cbvSrvUavDescSize;
	d3d.device->CreateShaderResourceView(resources.DXROutput, &textureSRVDesc, handle);
}

void RTAO::createFilterDescriptorHeaps(D3D12Global &d3d)
{
	// Describe the RTV heap
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
	rtvDesc.NumDescriptors = 1;
	rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	// Create the RTV heap
	HRESULT hr = d3d.device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&lowPassFilterRtvHeap));
	Utils::Validate(hr, L"Error: failed to create RTV descriptor heap!");

	rtvDescSize = d3d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;

	rtvHandle = lowPassFilterRtvHeap->GetCPUDescriptorHandleForHeapStart();

	// Create a RTV for temp buffer
	d3d.device->CreateRenderTargetView(tempScreenBuffer, nullptr, rtvHandle);
}

void RTAO::createFilterRootSignature(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler)
{
	// Load shaders for low pass filter
	lowPassFilterProgramVS = RtProgram(D3D12ShaderInfo(L"shaders\\RTAOLowPassFilter.hlsl", L"lowPassFilterVS", L"vs_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, lowPassFilterProgramVS);

	lowPassFilterXPassProgramPS = RtProgram(D3D12ShaderInfo(L"shaders\\RTAOLowPassFilter.hlsl", L"lowPassFilterXPassPS", L"ps_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, lowPassFilterXPassProgramPS);

	lowPassFilterYPassProgramPS = RtProgram(D3D12ShaderInfo(L"shaders\\RTAOLowPassFilter.hlsl", L"lowPassFilterYPassPS", L"ps_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, lowPassFilterYPassProgramPS);
	
	// Describe the root signature
	D3D12_DESCRIPTOR_RANGE ranges[2];

	ranges[0].BaseShaderRegister = 0;
	ranges[0].NumDescriptors = 1;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	ranges[1].BaseShaderRegister = 0;
	ranges[1].NumDescriptors = 3;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[1].OffsetInDescriptorsFromTableStart = 1;

	D3D12_ROOT_PARAMETER param0 = {};
	param0.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param0.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param0.DescriptorTable.NumDescriptorRanges = _countof(ranges);
	param0.DescriptorTable.pDescriptorRanges = ranges;

	const CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(
		0,                                 // shaderRegister s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,   // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	D3D12_ROOT_PARAMETER rootParams[1] = { param0 };

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = _countof(rootParams);
	rootDesc.pParameters = rootParams;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers = &linearClampSampler;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	
	// Create the root signature
	lowPassFilterRootSignature = D3D12::Create_Root_Signature(d3d, rootDesc);
}

void RTAO::createFilterPipelineStateObject(D3D12Global &d3d) {

	D3D12_INPUT_ELEMENT_DESC vertexDescription[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORDS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { vertexDescription, 2 };
	psoDesc.pRootSignature = lowPassFilterRootSignature;
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(lowPassFilterProgramVS.blob->GetBufferPointer()),
		lowPassFilterProgramVS.blob->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(lowPassFilterXPassProgramPS.blob->GetBufferPointer()),
		lowPassFilterXPassProgramPS.blob->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	HRESULT hr = d3d.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&lowPassFilterXPassPso));
	Utils::Validate(hr, L"Error: failed to create state object!");

	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(lowPassFilterYPassProgramPS.blob->GetBufferPointer()),
		lowPassFilterYPassProgramPS.blob->GetBufferSize()
	};

	hr = d3d.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&lowPassFilterYPassPso));
	Utils::Validate(hr, L"Error: failed to create state object!");
}

D3D12_CPU_DESCRIPTOR_HANDLE RTAO::getBackBufferView(UINT currentBufferIndex, D3D12Resources &resources) {

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = resources.rtvHeap->GetCPUDescriptorHandleForHeapStart();

	UINT renderTargetViewDescriptorSize = resources.rtvDescSize;

	renderTargetViewHandle.ptr += (renderTargetViewDescriptorSize * currentBufferIndex);

	return renderTargetViewHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE RTAO::getTempBufferView() {

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = lowPassFilterRtvHeap->GetCPUDescriptorHandleForHeapStart();

	return renderTargetViewHandle;
}

void RTAO::createRTAOBuffers(D3D12Global &d3d)
{
	// Describe the DXR output resource (texture)
	// Dimensions and format should match the swapchain
	// Initialize as a copy source, since we will copy this buffer's contents to the swapchain
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = d3d.width;
	desc.Height = d3d.height;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	// Create the buffer resource for AO
	HRESULT hr = d3d.device->CreateCommittedResource(&DefaultHeapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&AOOutputEven));
	Utils::Validate(hr, L"Error: failed to create DXR output buffer!");

	hr = d3d.device->CreateCommittedResource(&DefaultHeapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&AOOutputOdd));
	Utils::Validate(hr, L"Error: failed to create DXR output buffer!");
	
	// Create the temp buffer for low pass separable filter
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	hr = d3d.device->CreateCommittedResource(&DefaultHeapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&tempScreenBuffer));
	Utils::Validate(hr, L"Error: failed to create DXR output buffer!");

	// Create Ao samples buffer

	// Describe the texture
	desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	desc.Width = 360;
	desc.Height = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;

	// Create the texture resource
	hr = d3d.device->CreateCommittedResource(&DefaultHeapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&AOSamples));
	Utils::Validate(hr, L"Error: failed to create texture!");

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(AOSamples, 0, 1);

	// Describe the resource
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = uploadBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// Create the upload heap
	hr = d3d.device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&AOSamplesUploadHeap));
	Utils::Validate(hr, L"Error: failed to create texture upload heap!");

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = aoSamples;
	textureData.RowPitch = 360 * 3 * 4; //< in bytes
	textureData.SlicePitch = 1;

	// Schedule a copy from the upload heap to the Texture2D resource
	UpdateSubresources(d3d.cmdList, AOSamples, AOSamplesUploadHeap, 0, 0, 1, &textureData);

	// Transition the texture to a shader resource
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = AOSamples;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

	d3d.cmdList->ResourceBarrier(1, &barrier);
	
}

void RTAO::createRTAORayGenProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler)
{
	// Load and compile the ray generation shader
	rayGenShader = RtProgram(D3D12ShaderInfo(L"shaders\\RTAORayGen.hlsl", L"", L"lib_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, rayGenShader);

	// Describe the ray generation root signature
	D3D12_DESCRIPTOR_RANGE ranges[3];

	ranges[0].BaseShaderRegister = 0;
	ranges[0].NumDescriptors = 2;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	ranges[1].BaseShaderRegister = 0;
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].OffsetInDescriptorsFromTableStart = 2;

	ranges[2].BaseShaderRegister = 0;
	ranges[2].NumDescriptors = 5;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 3;

	D3D12_ROOT_PARAMETER param0 = {};
	param0.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param0.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param0.DescriptorTable.NumDescriptorRanges = _countof(ranges);
	param0.DescriptorTable.pDescriptorRanges = ranges;

	const CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(
		0,                                 // shaderRegister s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,   // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	D3D12_ROOT_PARAMETER rootParams[1] = { param0 };

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = _countof(rootParams);
	rootDesc.pParameters = rootParams;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers = &linearClampSampler;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	// Create the root signature
	rayGenShader.pRootSignature = D3D12::Create_Root_Signature(d3d, rootDesc);
}

void RTAO::createRTAOMissProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler)
{
	// Load and compile the miss shader
	missShader = RtProgram(D3D12ShaderInfo(L"shaders\\RTAOMiss.hlsl", L"", L"lib_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, missShader);

	// Create an empty root signature
	missShader.pRootSignature = D3D12::Create_Root_Signature(d3d, {});
}

void RTAO::createRTAOClosestHitProgram(D3D12Global &d3d, D3D12ShaderCompilerInfo &shaderCompiler)
{
	// Note: since all of our triangles are opaque, we will ignore the any hit program.

	// Load and compile the Closest Hit shader
	hitShader = HitProgram(L"Hit");
	hitShader.chs = RtProgram(D3D12ShaderInfo(L"shaders\\RTAOClosestHit.hlsl", L"", L"lib_6_3"));
	D3DShaders::Compile_Shader(shaderCompiler, hitShader.chs);

	// Describe the root signature
	D3D12_DESCRIPTOR_RANGE ranges[3];

	ranges[0].BaseShaderRegister = 0;
	ranges[0].NumDescriptors = 2;
	ranges[0].RegisterSpace = 0;
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;

	ranges[1].BaseShaderRegister = 0;
	ranges[1].NumDescriptors = 1;
	ranges[1].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].OffsetInDescriptorsFromTableStart = 2;

	ranges[2].BaseShaderRegister = 0;
	ranges[2].NumDescriptors = 5;
	ranges[2].RegisterSpace = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[2].OffsetInDescriptorsFromTableStart = 3;

	D3D12_ROOT_PARAMETER param0 = {};
	param0.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param0.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	param0.DescriptorTable.NumDescriptorRanges = _countof(ranges);
	param0.DescriptorTable.pDescriptorRanges = ranges;

	D3D12_ROOT_PARAMETER rootParams[1] = { param0 };

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = _countof(rootParams);
	rootDesc.pParameters = rootParams;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	// Create the root signature
	hitShader.chs.pRootSignature = D3D12::Create_Root_Signature(d3d, rootDesc);
}

void RTAO::createRTAOPipelineStateObject(D3D12Global &d3d)
{
	// Need 10 subobjects:
	// 1 for RGS program
	// 1 for Miss program
	// 1 for CHS program
	// 1 for Hit Group
	// 2 for RayGen Root Signature (root-signature and association)
	// 2 for Shader Config (config and association)
	// 1 for Global Root Signature
	// 1 for Pipeline Config	
	UINT index = 0;
	vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.resize(10);

	// Add state subobject for the RGS
	D3D12_EXPORT_DESC rgsExportDesc = {};
	rgsExportDesc.Name = L"RayGen_12";
	rgsExportDesc.ExportToRename = L"RayGen";
	rgsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	rgsLibDesc = {};
	rgsLibDesc.DXILLibrary.BytecodeLength = rayGenShader.blob->GetBufferSize();
	rgsLibDesc.DXILLibrary.pShaderBytecode = rayGenShader.blob->GetBufferPointer();
	rgsLibDesc.NumExports = 1;
	rgsLibDesc.pExports = &rgsExportDesc;

	D3D12_STATE_SUBOBJECT rgsState = {};
	rgsState.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	rgsState.pDesc = &rgsLibDesc;

	subobjects[index++] = rgsState;

	// Add state subobject for the Miss shader
	D3D12_EXPORT_DESC msExportDesc = {};
	msExportDesc.Name = L"Miss_5";
	msExportDesc.ExportToRename = L"Miss";
	msExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	msLibDesc = {};
	msLibDesc.DXILLibrary.BytecodeLength = missShader.blob->GetBufferSize();
	msLibDesc.DXILLibrary.pShaderBytecode = missShader.blob->GetBufferPointer();
	msLibDesc.NumExports = 1;
	msLibDesc.pExports = &msExportDesc;

	D3D12_STATE_SUBOBJECT ms = {};
	ms.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	ms.pDesc = &msLibDesc;

	subobjects[index++] = ms;

	// Add state subobject for the Closest Hit shader
	D3D12_EXPORT_DESC chsExportDesc = {};
	chsExportDesc.Name = L"ClosestHit_76";
	chsExportDesc.ExportToRename = L"ClosestHit";
	chsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	chsLibDesc = {};
	chsLibDesc.DXILLibrary.BytecodeLength = hitShader.chs.blob->GetBufferSize();
	chsLibDesc.DXILLibrary.pShaderBytecode = hitShader.chs.blob->GetBufferPointer();
	chsLibDesc.NumExports = 1;
	chsLibDesc.pExports = &chsExportDesc;

	D3D12_STATE_SUBOBJECT chs = {};
	chs.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	chs.pDesc = &chsLibDesc;

	subobjects[index++] = chs;

	// Add a state subobject for the hit group
	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = L"ClosestHit_76";
	hitGroupDesc.HitGroupExport = L"HitGroup";

	D3D12_STATE_SUBOBJECT hitGroup = {};
	hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	hitGroup.pDesc = &hitGroupDesc;

	subobjects[index++] = hitGroup;

	// Add a state subobject for the shader payload configuration
	D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
	shaderDesc.MaxPayloadSizeInBytes = 4; //< Just the T distance (float)
	shaderDesc.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

	D3D12_STATE_SUBOBJECT shaderConfigObject = {};
	shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	shaderConfigObject.pDesc = &shaderDesc;

	subobjects[index++] = shaderConfigObject;

	// Create a list of the shader export names that use the payload
	const WCHAR* shaderExports[] = { L"RayGen_12", L"Miss_5", L"HitGroup" };

	// Add a state subobject for the association between shaders and the payload
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
	shaderPayloadAssociation.NumExports = _countof(shaderExports);
	shaderPayloadAssociation.pExports = shaderExports;
	shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];

	D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
	shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;

	subobjects[index++] = shaderPayloadAssociationObject;

	// Add a state subobject for the shared root signature 
	D3D12_STATE_SUBOBJECT rayGenRootSigObject = {};
	rayGenRootSigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	rayGenRootSigObject.pDesc = &rayGenShader.pRootSignature;

	subobjects[index++] = rayGenRootSigObject;

	// Create a list of the shader export names that use the root signature
	const WCHAR* rootSigExports[] = { L"RayGen_12", L"HitGroup", L"Miss_5" };

	// Add a state subobject for the association between the RayGen shader and the RayGen root signature
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rayGenShaderRootSigAssociation = {};
	rayGenShaderRootSigAssociation.NumExports = _countof(rootSigExports);
	rayGenShaderRootSigAssociation.pExports = rootSigExports;
	rayGenShaderRootSigAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];

	D3D12_STATE_SUBOBJECT rayGenShaderRootSigAssociationObject = {};
	rayGenShaderRootSigAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	rayGenShaderRootSigAssociationObject.pDesc = &rayGenShaderRootSigAssociation;

	subobjects[index++] = rayGenShaderRootSigAssociationObject;

	D3D12_STATE_SUBOBJECT globalRootSig;
	globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	globalRootSig.pDesc = &missShader.pRootSignature;

	subobjects[index++] = globalRootSig;

	// Add a state subobject for the ray tracing pipeline config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
	pipelineConfig.MaxTraceRecursionDepth = 1;

	D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
	pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	pipelineConfigObject.pDesc = &pipelineConfig;

	subobjects[index++] = pipelineConfigObject;

	// Describe the Ray Tracing Pipeline State Object
	D3D12_STATE_OBJECT_DESC pipelineDesc = {};
	pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	pipelineDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
	pipelineDesc.pSubobjects = subobjects.data();

	// Create the RT Pipeline State Object (RTPSO)
	HRESULT hr = d3d.device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&rtaoPipelineStateObject));
	Utils::Validate(hr, L"Error: failed to create state object!");

	// Get the RTPSO properties
	hr = rtaoPipelineStateObject->QueryInterface(IID_PPV_ARGS(&rtaoPipelineStateObjectInfo));
	Utils::Validate(hr, L"Error: failed to get RTPSO info object!");
}
