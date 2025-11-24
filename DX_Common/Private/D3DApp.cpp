#include "pch.h"

#include "D3DApp.h"
#include "D3DUtil.h"
#include "directx/d3dx12.h"

D3DApp::~D3DApp()
{
	// The destructor releases the COM interfaces the D3DApp acquires, 
	// and flushes the command queue.
	// The reason we need to flush the command queue in the destructor is that
	// we need to wait until the GPU is done processing the commands in the queue
	// before we destroy any resources the GPU is still referencing.
	// Otherwise, the GPU might crash when the application exits.
	if (m_d3dDevice != nullptr)
		FlushCommandQueue();
}

HINSTANCE D3DApp::AppInst()const
{
	// returns a copy of the application instance handle
	return m_hAppInst;
}

HWND D3DApp::MainWnd()const
{
	// returns a copy of the main window handle.
	return m_hMainWnd;
}

float D3DApp::AspectRatio()const
{
	// the ratio of the back buffer width to its height
	return static_cast<float>(m_ClientWidth) / m_ClientHeight;
}

bool D3DApp::Get4xMsaaState() const
{
	// returns true is 4X MSAA is enabled and false otherwise.
	return m_4xMsaaState;
}

void D3DApp::Set4xMsaaState(bool value)
{
	// Enables/disables 4X MSAA.
	if (m_4xMsaaState != value)
	{
		m_4xMsaaState = value;

		// Recreate the swapchain and buffers with new multisample settings.
		CreateSwapChain();
		OnResize();
	}
}

int D3DApp::Run()
{
	MSG msg = { 0 };

	m_Timer.Reset();

	while (msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// Otherwise, do animation/game stuff.
		else
		{
			m_Timer.Tick();

			if (!m_AppPaused)
			{
				CalculateFrameStats();
				Update(m_Timer);
				Draw(m_Timer);
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return (int)msg.wParam;
}

bool D3DApp::Initialize()
{
	if (!InitMainWindow())
		return false;

	if (!InitDirect3D())
		return false;

	// Do the initial resize code.
	OnResize();

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

void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
	// RTV Heap
	// Need s_SwapChainBufferCount render target views
	// to describe the buffer resources in the swap chain we will render into
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = s_SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc,
		IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	// DSV Heap
	// Need one depth/stencil view
	// to use as the depth/stencil buffer for depth testing
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc,
		IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}

ID3D12Resource* D3DApp::CurrentBackBuffer() const
{
	return m_SwapChainBuffer[m_CurrentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView() const
{
	// CD3DX12 constructor to offset to the RTV of the current back buffer
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_RtvHeap->GetCPUDescriptorHandleForHeapStart(), // handle start
		m_CurrentBackBuffer, // index to offset
		m_RtvDescriptorSize); // byte size of each descriptor
}

D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView() const
{
	// CD3DX12 constructor to get the DSV (only one, so no offset needed)
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}