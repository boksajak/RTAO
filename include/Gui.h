#pragma once

#include "Graphics.h"

class Gui {
public:

	void Init(D3D12Global &d3d, HWND window);

	void Update();

	void Render(D3D12Global &d3d, D3D12Resources &resources);

	bool CallWndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void Destroy();

	void Text(const char* text, double x);
	void SliderFloat(const char* label, float* v, float min, float max);
	void SliderInt(const char* label, int* v, int min, int max);
	void Combo(const char* label, int* currentItem, const char* options);

private:

	struct FrameContext
	{
		ID3D12CommandAllocator* CommandAllocator;
		UINT64                  FenceValue;
	};

	UINT                         g_frameIndex = 0;
	HANDLE                       g_hSwapChainWaitableObject = NULL;
	static int const			 NUM_FRAMES_IN_FLIGHT = 3;
	FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};

	ID3D12DescriptorHeap*        g_pd3dSrvDescHeap = NULL;

};