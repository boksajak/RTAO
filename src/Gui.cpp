#include "Gui.h"


#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Gui::Init(D3D12Global &d3d, HWND window) {

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (d3d.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
			return;
	}

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init((void*)window);
	ImGui_ImplDX12_Init(d3d.device, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

void Gui::Update()
{
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("RTAO");
}

void Gui::Render(D3D12Global &d3d, D3D12Resources &resources) {

	ImGui::End();

	UINT backBufferIdx = d3d.swapChain->GetCurrentBackBufferIndex();
	d3d.cmdAlloc[d3d.frameIndex]->Reset();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = d3d.backBuffer[d3d.frameIndex];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = resources.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	UINT renderTargetViewDescriptorSize = resources.rtvDescSize;
	renderTargetViewHandle.ptr += (renderTargetViewDescriptorSize * d3d.frameIndex);

	d3d.cmdList->Reset(d3d.cmdAlloc[d3d.frameIndex], NULL);
	d3d.cmdList->ResourceBarrier(1, &barrier);
	d3d.cmdList->OMSetRenderTargets(1, &renderTargetViewHandle, FALSE, NULL);
	d3d.cmdList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d.cmdList);
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	d3d.cmdList->ResourceBarrier(1, &barrier);
	d3d.cmdList->Close();

	d3d.cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&d3d.cmdList);

}

bool Gui::CallWndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}

void Gui::Text(const char* text, double x) {
	ImGui::Text(text, x);
}

void Gui::SliderFloat(const char* label, float* v, float min, float max) {
	ImGui::SliderFloat(label, v, min, max);
}

void Gui::SliderInt(const char* label, int* v, int min, int max) {
	ImGui::SliderInt(label, v, min, max);
}

void Gui::Combo(const char* label, int* currentItem, const char* options) {
	ImGui::Combo("Output Mode", currentItem, options);
}

void Gui::Destroy() {
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}
