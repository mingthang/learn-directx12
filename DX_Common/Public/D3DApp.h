#pragma once

#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h> // DXGI 1.6

class D3DApp
{
public:

	D3DApp();

protected:

	bool Init();
	bool InitDirect3D();

	void CreateCommandObjects();
	void CreateSwapChain();
	void CreateRtvAndDsvDescriptorHeaps();

	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

protected:

	// Main application window handle
	HWND m_hMainWnd = nullptr; // main window handle
	int m_ClientWidth = 800;
	int m_ClientHeight = 600;

	// Direct3D objects
	Microsoft::WRL::ComPtr<IDXGIFactory7> m_dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> m_d3dDevice;
	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_DirectCmdListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;

	// Descriptor Heaps
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;

	// Swap chain back buffers
	static const int s_SwapChainBufferCount = 2;
	int m_CurrentBackBuffer = 0;

	// Descriptor sizes
	UINT m_RtvDescriptorSize = 0;
	UINT m_DsvDescriptorSize = 0;
	UINT m_CbvSrvUavDescriptorSize = 0;

	// Set true to use 4X MSAA. The default is false.
	bool m_4xMsaaState = false;    // 4X MSAA enabled
	UINT m_4xMsaaQuality = 0;      // quality level of 4X MSAA

	// Derived class should set these in derived constructor to customize starting values.
	DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};
