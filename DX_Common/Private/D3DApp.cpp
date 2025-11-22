#include "pch.h"

#include "D3DApp.h"
#include "D3DUtil.h"

D3DApp::D3DApp()
{
}

bool D3DApp::Init()
{
	if (!InitDirect3D())
	{
		return false;
	}

	return true;
}

bool D3DApp::InitDirect3D()
{
	#if defined(DEBUG) || defined(_DEBUG)
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
	#endif
	
	// == Create DXGI factory ==
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));
	
	// == Create Direct3D 12 device ==
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_12_2,
		IID_PPV_ARGS(&m_d3dDevice));

	// Fallback to WARP device
	if (FAILED(hardwareResult))
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> p_WarpAdapter;
		ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&p_WarpAdapter)));
		ThrowIfFailed(D3D12CreateDevice(
			p_WarpAdapter.Get(),
			D3D_FEATURE_LEVEL_12_2,
			IID_PPV_ARGS(&m_d3dDevice)));
	}

	// == Create Fence and Descriptor Sizes ==
	
	// 1. Fence object for CPU/GPU synchronization
	ThrowIfFailed(m_d3dDevice->CreateFence(
		0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));

	// 2. Descriptor sizes can vary across GPUs. Query and cache this information for working with various descriptor types when we need
	m_RtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvUavDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// == Check 4X MSAA Quality Support ==
	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.

	// Why 4X MSSA?
		// 1. Good improvement in image quality without too much performance impact
		// 2. 4X is widely supported on most hardware esp all Direct3D 11 capable devices
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_d3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m_4xMsaaQuality > 0 && "Unexpected Max MSAA sample count"); // because 4X MSAA is always supported, the returned quality should always be greater than 0; 
																	   // therefore, we assert that this is the case.

	CreateCommandObjects();
	CreateSwapChain();

	return true;
}

void D3DApp::CreateCommandObjects()
{
	// == Create Command Queue and Command List ==
	// A command queue is represented by the ID3D12CommandQueue interace
	// A command allocator is represented by the ID3D12CommandAllocator interface
	// A command list is represented by the ID3D12GraphicsCommandList interface
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_d3dDevice->CreateCommandQueue(
		&queueDesc,
		IID_PPV_ARGS(&m_CommandQueue)));

	ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_DirectCmdListAlloc.GetAddressOf()))); // why not &m_DirectCmdListAlloc?
	
	ThrowIfFailed(m_d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_DirectCmdListAlloc.Get(), // ID3D12CommandAllocator*
		nullptr, // initial pipeline state object (we don't set for now b/c we don't need a valid pipeline state object yet)
		IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	// Start off in a closed state. This is because the first time we 
	// refer to the command list we will Reset it, and it needs to be 
	// closed before calling Reset.
	m_CommandList->Close();
}

void D3DApp::CreateSwapChain()
{
	// To create a swap chain we need to use the IDXGIFactory object
	// that we created earlier. The swap chain needs to be associated
	// with a window handle (HWND) so that DXGI knows where to put the
	// images that are being rendered. The swap chain is created based
	// on a DXGI_SWAP_CHAIN_DESC structure.
	// 
	// Note: Swap chain creation modifies the command queue to associate
	// it with the swap chain. So after this call, the command queue
	// should not be used to execute command lists until after the
	// swap chain has been presented for the first time.

	// Observe that this function has been designed so that it can be called multiple times. 
	// It will destroy the old swap chain before creating the new one. 
	// This allows us to recreate the swap chain with different settings; 
	// in particular, we can change the multisampling settings at runtime.

	// Release the previous swapchain we will be recreating
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_ClientWidth;
	sd.BufferDesc.Height = m_ClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = m_BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = s_SwapChainBufferCount;
	sd.OutputWindow = m_hMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	// Note: Swap chain uses queue to perform flush
	ThrowIfFailed(m_dxgiFactory->CreateSwapChain(
		m_CommandQueue.Get(),
		&sd,
		m_SwapChain.GetAddressOf()));
}