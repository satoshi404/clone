#include <backend/gpu.hpp>
#include <backend/gpu/backend.hpp>
#include <backend/window.hpp>

#include <core/debug.hpp>

#include <cstring>

#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if GRAPHICS_API_D3D12 || 1

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

namespace Window
{
	extern HWND hwnd;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core device/swapchain state
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ID3D12Device *device = nullptr;
static ID3D12CommandQueue *commandQueue = nullptr;
static IDXGIFactory4 *dxgiFactory = nullptr;
static IDXGISwapChain3 *swapChain = nullptr;
static ID3D12DescriptorHeap *rtvHeap = nullptr;
static ID3D12Resource *renderTargets[2] = { nullptr, nullptr };
static ID3D12CommandAllocator *commandAllocator = nullptr;
static ID3D12GraphicsCommandList *commandList = nullptr; // the native D3D12 list — not to be confused with Gpu's CommandList*
static ID3D12Fence *fence = nullptr;

static u64 fenceValue = 0;
static HANDLE fenceEvent = nullptr;
static UINT rtvDescriptorSize = 0;
static UINT frameIndex = 0;

static D3D12_VIEWPORT viewport = {};
static D3D12_RECT scissorRect = {};

// Optional depth buffer, sized once at init. Bound whenever a RenderPass asks for a depth attachment.
static ID3D12Resource *depthBuffer = nullptr;
static ID3D12DescriptorHeap *dsvHeap = nullptr;
static const DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;

// One root signature shared by every pipeline: 8 root CBVs (b0..b7), no descriptor tables.
// This is deliberately the simplest thing that works for uniform buffers. Textures/samplers in
// DescriptorSetDesc are accepted by the API but not wired up yet — that needs an SRV/sampler
// descriptor heap, which is a bigger addition (see the TODO in command_list_execute below).
static ID3D12RootSignature *rootSignature = nullptr;
static const UINT ROOT_CBV_COUNT = 8;
static const UINT ROOT_PARAM_TEXTURE_TABLE = ROOT_CBV_COUNT; // root param 8: one shared SRV descriptor table (t0)

// Textures share ONE fixed SRV slot in the root signature (t0) and ONE static sampler (s0, linear
// wrap) baked directly into the root signature — no runtime sampler binding needed, matching how
// DescriptorType::Sampler has no backing resource handle in gpu.hpp. This means only one texture can
// be bound per draw call today; binding a second texture at a different slot would need its own
// descriptor range/root parameter, which isn't wired up yet.
static ID3D12DescriptorHeap *srvHeap = nullptr;
static UINT srvDescriptorSize = 0;
static const UINT MAX_TEXTURES = 64;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource pools
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BufferResource
{
	ID3D12Resource *resource;
	void *mappedPtr; // persistently mapped (see NOTE in buffer_create)
	u32 size;
	BufferType type;
};

struct ShaderResource
{
	ID3DBlob *blob;
};

struct PipelineResource
{
	ID3D12PipelineState *pso;
	D3D_PRIMITIVE_TOPOLOGY topology; // for IASetPrimitiveTopology at draw time
	u32 vertexStride;                // from VertexLayout.stride, needed to build the VBV at draw time
};

struct DescriptorSetResource
{
	DescriptorBinding bindings[ROOT_CBV_COUNT];
	u32 bindingCount;
};

struct TextureResource
{
	ID3D12Resource *resource;
	u32 width;
	u32 height;
};

static HandlePool<BufferResource, 256> g_bufferPool;
static HandlePool<ShaderResource, 128> g_shaderPool;
static HandlePool<PipelineResource, 64> g_pipelinePool;
static HandlePool<DescriptorSetResource, 128> g_descriptorSetPool;
static HandlePool<TextureResource, MAX_TEXTURES> g_texturePool;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enum translation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DXGI_FORMAT to_dxgi_format(VertexFormat format)
{
	switch (format)
	{
		case VertexFormat::Float1:     return DXGI_FORMAT_R32_FLOAT;
		case VertexFormat::Float2:     return DXGI_FORMAT_R32G32_FLOAT;
		case VertexFormat::Float3:     return DXGI_FORMAT_R32G32B32_FLOAT;
		case VertexFormat::Float4:     return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case VertexFormat::UByte4Norm: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	return DXGI_FORMAT_UNKNOWN;
}

static D3D12_CULL_MODE to_d3d_cull_mode(CullMode mode)
{
	switch (mode)
	{
		case CullMode::None:  return D3D12_CULL_MODE_NONE;
		case CullMode::Front: return D3D12_CULL_MODE_FRONT;
		case CullMode::Back:  return D3D12_CULL_MODE_BACK;
	}
	return D3D12_CULL_MODE_BACK;
}

static D3D12_COMPARISON_FUNC to_d3d_compare_func(CompareFunc func)
{
	switch (func)
	{
		case CompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
		case CompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
		case CompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
		case CompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
	}
	return D3D12_COMPARISON_FUNC_LESS;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE to_d3d_topology_type(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::TriangleList:
		case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PrimitiveTopology::LineList:      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	}
	return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

static D3D_PRIMITIVE_TOPOLOGY to_d3d_primitive_topology(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	}
	return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend lifecycle
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Backend::init()
{
	UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
	// Enable the D3D12 debug layer for development verification
	ID3D12Debug* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		debugController->Release();
	}
#endif

	// Create factory
	HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create DXGI factory");
		return false;
	}

	// Init D3D12 device
	hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create D3D12 device");
		return false;
	}

	// Create swap chain descriptor
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = WindowConfig::get_width();
	swapChainDesc.Height = WindowConfig::get_height();
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create command queue");
		return false;
	}

	// Create swap chain
	IDXGISwapChain1 *tempSwapChain = nullptr;
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, Window::hwnd, &swapChainDesc, nullptr, nullptr, &tempSwapChain);
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create swap chain");
		return false;
	}

	// Query for IDXGISwapChain3 interface
	hr = tempSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain));
	tempSwapChain->Release();
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to query IDXGISwapChain3 interface");
		return false;
	}

	// Create render target views descriptor heap
	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create RTV descriptor heap");
		return false;
	}

	// Create render target views for each buffer
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < 2; ++i)
	{
		hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
		if (FAILED(hr))
		{
			TerminalDebug::println(PrintColorType_Red, "Failed to get swap chain buffer");
			return false;
		}
		device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);
		rtvHandle.ptr += rtvDescriptorSize;
	}

	// Create command allocator
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create command allocator");
		return false;
	}

	// Create command list
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create command list");
		return false;
	}

	// Command lists are created in an "open" state. Close it until the rendering loop begins.
	hr = commandList->Close();
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to close initial command list");
		return false;
	}

	// Create fence
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create fence");
		return false;
	}

	// Create fence event
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr)
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create fence event");
		return false;
	}

	// Set initial frame index
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Depth buffer (created unconditionally; only bound if a RenderPass asks for it)
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create DSV descriptor heap");
		return false;
	}

	D3D12_HEAP_PROPERTIES depthHeapProps = {};
	depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = WindowConfig::get_width();
	depthDesc.Height = WindowConfig::get_height();
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DEPTH_FORMAT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = DEPTH_FORMAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	hr = device->CreateCommittedResource(&depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthBuffer));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create depth buffer");
		return false;
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DEPTH_FORMAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthBuffer, &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());

	// Global root signature: N root CBVs (b0..b7) + one shared SRV descriptor table (t0) for
	// textures + one static sampler (s0). See comment near ROOT_PARAM_TEXTURE_TABLE.
	D3D12_ROOT_PARAMETER rootParams[ROOT_CBV_COUNT + 1] = {};
	for (UINT i = 0; i < ROOT_CBV_COUNT; ++i)
	{
		rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParams[i].Descriptor.ShaderRegister = i;
		rootParams[i].Descriptor.RegisterSpace = 0;
	}

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0; // t0
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[ROOT_PARAM_TEXTURE_TABLE].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[ROOT_PARAM_TEXTURE_TABLE].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParams[ROOT_PARAM_TEXTURE_TABLE].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[ROOT_PARAM_TEXTURE_TABLE].DescriptorTable.pDescriptorRanges = &srvRange;

	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc.ShaderRegister = 0; // s0
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters = ROOT_CBV_COUNT + 1;
	rootSigDesc.pParameters = rootParams;
	rootSigDesc.NumStaticSamplers = 1;
	rootSigDesc.pStaticSamplers = &samplerDesc;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob *signatureBlob = nullptr;
	ID3DBlob *errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { TerminalDebug::println(PrintColorType_Red, (const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
		TerminalDebug::println(PrintColorType_Red, "Failed to serialize root signature");
		return false;
	}

	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	signatureBlob->Release();
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create root signature");
		return false;
	}

	viewport = { 0.0f, 0.0f, static_cast<float>(WindowConfig::get_width()), static_cast<float>(WindowConfig::get_height()), 0.0f, 1.0f };
	scissorRect = { 0, 0, static_cast<LONG>(WindowConfig::get_width()), static_cast<LONG>(WindowConfig::get_height()) };

	// SRV heap for textures: shader-visible, fixed capacity. Each texture gets one persistent slot
	// (heap index == its pool index), created/overwritten in Backend::texture_create.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = MAX_TEXTURES;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create SRV descriptor heap");
		return false;
	}
	srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	g_bufferPool.init();
	g_shaderPool.init();
	g_pipelinePool.init();
	g_descriptorSetPool.init();
	g_texturePool.init();

	TerminalDebug::println(PrintColorType_Green, "D3D12 backend initialized successfully");
	return true;
}

void Backend::info()
{
	DXGI_ADAPTER_DESC1 desc;
    for ( int index = 0;; index++ )
    {
        IDXGIAdapter1* adapter = nullptr;

		HRESULT hr = dxgiFactory->EnumAdapters1( index, &adapter );
		if ( hr ==  DXGI_ERROR_NOT_FOUND ) break;

        if ( SUCCEEDED( adapter->GetDesc1( &desc ) ) )
        {
            TerminalDebug::println( "Gpu[%d] name: %ls", index, desc.Description );
        }

		adapter->Release();
    }
}

void Backend::free()
{
	// Ensure the GPU has fully completed all scheduled operations before sweeping memory
	if (fence && commandQueue)
	{
		fenceValue++;
		commandQueue->Signal(fence, fenceValue);
		if (fence->GetCompletedValue() < fenceValue)
		{
			fence->SetEventOnCompletion(fenceValue, fenceEvent);
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	}

	if (fenceEvent) { CloseHandle(fenceEvent); fenceEvent = nullptr; }
	if (fence) { fence->Release(); fence = nullptr; }

	if (dxgiFactory) { dxgiFactory->Release();  dxgiFactory = nullptr; }

	if (rootSignature) { rootSignature->Release(); rootSignature = nullptr; }

	if (depthBuffer) { depthBuffer->Release(); depthBuffer = nullptr; }
	if (dsvHeap) { dsvHeap->Release(); dsvHeap = nullptr; }

	if (commandList) { commandList->Release(); commandList = nullptr; }
	if (commandAllocator) { commandAllocator->Release(); commandAllocator = nullptr; }

	for (UINT i = 0; i < 2; ++i)
	{
		if (renderTargets[i]) { renderTargets[i]->Release(); renderTargets[i] = nullptr; }
	}

	if (rtvHeap) { rtvHeap->Release(); rtvHeap = nullptr; }
	if (srvHeap) { srvHeap->Release(); srvHeap = nullptr; }
	if (swapChain) { swapChain->Release(); swapChain = nullptr; }
	if (commandQueue) { commandQueue->Release(); commandQueue = nullptr; }
	if (device) { device->Release(); device = nullptr; }

	TerminalDebug::println(PrintColorType_Yellow, "D3D12 backend freed successfully");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render pass
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Backend::render_pass_begin(RenderPass *renderPass)
{
	commandAllocator->Reset();
	// Pipeline state is set per-draw via CommandType::SetPipeline (see command_list_execute), not here —
	// a render pass can now contain draws with different pipelines.
	commandList->Reset(commandAllocator, nullptr);

	commandList->SetGraphicsRootSignature(rootSignature);

	// Precisa ser setado sempre que a command list é resetada — SetGraphicsRootDescriptorTable
	// abaixo (em SetDescriptorSet) depende de um heap shader-visible já vinculado.
	ID3D12DescriptorHeap *heaps[] = { srvHeap };
	commandList->SetDescriptorHeaps(1, heaps);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = renderTargets[frameIndex];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += frameIndex * rtvDescriptorSize;

	if (renderPass->depthAttachmentCount > 0)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	}
	else
	{
		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
	}

	float clearColor[4] = { renderPass->clearColor[0], renderPass->clearColor[1], renderPass->clearColor[2], renderPass->clearColor[3] };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
}

void Backend::render_pass_end(RenderPass *renderPass)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = renderTargets[frameIndex];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	commandList->Close();
	ID3D12CommandList *ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, ppCommandLists);

	swapChain->Present(1, 0);

	const u64 currentFenceValue = ++fenceValue;
	commandQueue->Signal(fence, currentFenceValue);
	if (fence->GetCompletedValue() < currentFenceValue)
	{
		fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	frameIndex = swapChain->GetCurrentBackBufferIndex();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffers
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BufferHandle Backend::buffer_create(const BufferDesc &desc)
{
	Handle h = g_bufferPool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Buffer pool exhausted");
		return { HANDLE_INVALID };
	}

	// NOTE: every buffer lives in an UPLOAD heap, whether it's a "Static" vertex buffer or a
	// "Dynamic" uniform buffer. That keeps buffer_update a plain memcpy for both, at the cost of
	// the GPU reading through system memory instead of VRAM. Fine while geometry is tiny; once
	// static meshes get big, give BufferUsage::Static a DEFAULT-heap + upload-and-copy path instead.
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = desc.size;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create buffer");
		g_bufferPool.release(h);
		return { HANDLE_INVALID };
	}

	void *mappedPtr = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	resource->Map(0, &readRange, &mappedPtr);
	if (desc.data) memcpy(mappedPtr, desc.data, desc.size);

	BufferResource *res = g_bufferPool.get(h);
	res->resource = resource;
	res->mappedPtr = mappedPtr;
	res->size = desc.size;
	res->type = desc.type;

	return { h };
}

void Backend::buffer_update(BufferHandle buffer, u32 offset, u32 size, const void *data)
{
	BufferResource *res = g_bufferPool.get(buffer.handle);
	if (!res)
	{
		TerminalDebug::println(PrintColorType_Red, "buffer_update: invalid handle");
		return;
	}
	memcpy(static_cast<u8*>(res->mappedPtr) + offset, data, size);
}

void Backend::buffer_destroy(BufferHandle buffer)
{
	BufferResource *res = g_bufferPool.get(buffer.handle);
	if (res && res->resource)
	{
		res->resource->Unmap(0, nullptr);
		res->resource->Release();
	}
	g_bufferPool.release(buffer.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Textures
//
// UNLIKE buffers, a texture can't just live on an upload heap with ROW_MAJOR layout and be sampled
// directly — GPUs need an optimal/tiled layout to sample efficiently, and most drivers reject (or
// simply fail to create) an SRV-sampleable Texture2D placed straight on an upload heap. So textures
// need the "real" D3D12 upload path: a DEFAULT-heap texture (opaque/optimal layout) + an UPLOAD-heap
// staging buffer + a GPU-side CopyTextureRegion + a barrier to PIXEL_SHADER_RESOURCE. This reuses the
// same commandList/commandAllocator/fence the render loop uses, since texture_create only ever runs
// during setup, never nested inside a render_pass_begin/end block.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TextureHandle Backend::texture_create(const TextureDesc &desc)
{
	Handle h = g_texturePool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Texture pool exhausted");
		return { HANDLE_INVALID };
	}

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = desc.width;
	texDesc.Height = desc.height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // deixa o driver escolher o layout tiled ótimo

	D3D12_HEAP_PROPERTIES defaultHeapProps = {};
	defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create texture (default heap)");
		g_texturePool.release(h);
		return { HANDLE_INVALID };
	}

	// Footprint dita exatamente como o driver quer os dados de origem organizados (RowPitch pode
	// ter padding além de width*4) — consultamos em vez de assumir um layout tightly-packed.
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 uploadBufferSize = 0;
	device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadBufferSize);

	D3D12_HEAP_PROPERTIES uploadHeapProps = {};
	uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC uploadBufferDesc = {};
	uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadBufferDesc.Width = uploadBufferSize;
	uploadBufferDesc.Height = 1;
	uploadBufferDesc.DepthOrArraySize = 1;
	uploadBufferDesc.MipLevels = 1;
	uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	uploadBufferDesc.SampleDesc.Count = 1;
	uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // um buffer comum, isso É válido pra buffer

	ID3D12Resource *uploadBuffer = nullptr;
	hr = device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create texture upload buffer");
		resource->Release();
		g_texturePool.release(h);
		return { HANDLE_INVALID };
	}

	void *mappedPtr = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	uploadBuffer->Map(0, &readRange, &mappedPtr);
	if (desc.data)
	{
		u8 *dstBase = static_cast<u8*>(mappedPtr);
		const u8 *srcBase = static_cast<const u8*>(desc.data);
		const u32 srcRowBytes = desc.width * 4; // input é sempre RGBA8 tightly-packed

		for (UINT row = 0; row < desc.height; ++row)
		{
			memcpy(dstBase + row * footprint.Footprint.RowPitch, srcBase + row * srcRowBytes, srcRowBytes);
		}
	}
	uploadBuffer->Unmap(0, nullptr);

	// Cópia GPU-side do staging buffer pra textura de layout ótimo. Reaproveita o command
	// allocator/list/fence globais — seguro porque isso roda só no setup, nunca dentro de um
	// render_pass_begin/end ativo (que faria seu próprio Reset() logo em seguida sem conflito).
	commandAllocator->Reset();
	commandList->Reset(commandAllocator, nullptr);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = resource;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadBuffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

	commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);

	commandList->Close();
	ID3D12CommandList *ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, ppCommandLists);

	// Espera síncrona — a mais simples e correta aqui, já que carregamento de textura só
	// acontece durante o setup (não é um caminho hot por frame).
	const u64 uploadFenceValue = ++fenceValue;
	commandQueue->Signal(fence, uploadFenceValue);
	if (fence->GetCompletedValue() < uploadFenceValue)
	{
		fence->SetEventOnCompletion(uploadFenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	uploadBuffer->Release(); // seguro: já esperamos a GPU terminar a cópia acima

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += (SIZE_T)h.index * srvDescriptorSize;
	device->CreateShaderResourceView(resource, &srvDesc, srvHandle);

	TextureResource *res = g_texturePool.get(h);
	res->resource = resource;
	res->width = desc.width;
	res->height = desc.height;

	return { h };
}

void Backend::texture_destroy(TextureHandle texture)
{
	TextureResource *res = g_texturePool.get(texture.handle);
	if (res && res->resource) res->resource->Release();
	g_texturePool.release(texture.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shaders
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ShaderHandle Backend::shader_create(const ShaderDesc &desc)
{
	Handle h = g_shaderPool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Shader pool exhausted");
		return { HANDLE_INVALID };
	}

	// D3D12 always compiles from HLSL text here; a backend that only accepts precompiled bytecode
	// (Vulkan + SPIR-V, mainly) would branch on this instead of calling D3DCompile.
	const char *target = (desc.stage == ShaderStage::Vertex)   ? "vs_5_0"
	                    : (desc.stage == ShaderStage::Fragment) ? "ps_5_0"
	                    :                                          "cs_5_0";

	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	UINT codeSize = desc.codeSize ? desc.codeSize : static_cast<UINT>(strlen(static_cast<const char*>(desc.code)));

	ID3DBlob *blob = nullptr;
	ID3DBlob *errorBlob = nullptr;

	const char* entryPoint = desc.entryPoint ? desc.entryPoint :
                         (desc.stage == ShaderStage::Vertex ? "VSMain" : "PSMain");

	TerminalDebug::println( PrintColorType_Cyan, entryPoint );

	HRESULT hr = D3DCompile(desc.code, codeSize, nullptr, nullptr, nullptr, entryPoint, target, compileFlags, 0, &blob, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { TerminalDebug::println(PrintColorType_Red, (const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
		TerminalDebug::println(PrintColorType_Red, "Failed to compile shader");
		g_shaderPool.release(h);
		return { HANDLE_INVALID };
	}

	g_shaderPool.get(h)->blob = blob;
	return { h };
}

void Backend::shader_destroy(ShaderHandle shader)
{
	ShaderResource *res = g_shaderPool.get(shader.handle);
	if (res && res->blob) res->blob->Release();
	g_shaderPool.release(shader.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pipelines
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PipelineHandle Backend::pipeline_create(const PipelineDesc &desc)
{
	Handle h = g_pipelinePool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Pipeline pool exhausted");
		return { HANDLE_INVALID };
	}

	ShaderResource *vs = g_shaderPool.get(desc.vertexShader.handle);
	ShaderResource *ps = g_shaderPool.get(desc.fragmentShader.handle);
	if (!vs || !ps)
	{
		TerminalDebug::println(PrintColorType_Red, "pipeline_create: invalid shader handle");
		g_pipelinePool.release(h);
		return { HANDLE_INVALID };
	}

	// Convention: every HLSL vertex shader takes inputs named ATTRIB0, ATTRIB1, ... matching
	// VertexAttribute::location, since D3D semantics are named strings, not raw locations like
	// GLSL/SPIR-V. Keep this in mind when writing/cross-compiling shaders for this backend.
	D3D12_INPUT_ELEMENT_DESC inputElements[8] = {};
	for (u32 i = 0; i < desc.vertexLayout.attributeCount; ++i)
	{
		const VertexAttribute &attr = desc.vertexLayout.attributes[i];
		inputElements[i].SemanticName = "ATTRIB";
		inputElements[i].SemanticIndex = attr.location;
		inputElements[i].Format = to_dxgi_format(attr.format);
		inputElements[i].InputSlot = 0;
		inputElements[i].AlignedByteOffset = attr.offset;
		inputElements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

		inputElements[i].InstanceDataStepRate = 0;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElements, desc.vertexLayout.attributeCount };
	psoDesc.pRootSignature = rootSignature;
	psoDesc.VS = { vs->blob->GetBufferPointer(), vs->blob->GetBufferSize() };
	psoDesc.PS = { ps->blob->GetBufferPointer(), ps->blob->GetBufferSize() };

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = to_d3d_cull_mode(desc.cullMode);
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	if (desc.blendEnable)
	{
		psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	}

	psoDesc.DepthStencilState.DepthEnable = desc.depthTestEnable;
	psoDesc.DepthStencilState.DepthWriteMask = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = to_d3d_compare_func(desc.depthCompare);
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = to_d3d_topology_type(desc.topology);
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = desc.depthTestEnable ? DEPTH_FORMAT : DXGI_FORMAT_UNKNOWN;
	psoDesc.SampleDesc.Count = 1;

	ID3D12PipelineState *pso = nullptr;
	HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
	if (FAILED(hr))
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to create graphics pipeline state");
		g_pipelinePool.release(h);
		return { HANDLE_INVALID };
	}

	PipelineResource *res = g_pipelinePool.get(h);
	res->pso = pso;
	res->topology = to_d3d_primitive_topology(desc.topology);
	res->vertexStride = desc.vertexLayout.stride;

	return { h };
}

void Backend::pipeline_destroy(PipelineHandle pipeline)
{
	PipelineResource *res = g_pipelinePool.get(pipeline.handle);
	if (res && res->pso) res->pso->Release();
	g_pipelinePool.release(pipeline.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor sets
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DescriptorSetHandle Backend::descriptor_set_create(const DescriptorSetDesc &desc)
{
	Handle h = g_descriptorSetPool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Descriptor set pool exhausted");
		return { HANDLE_INVALID };
	}

	DescriptorSetResource *res = g_descriptorSetPool.get(h);
	res->bindingCount = desc.bindingCount < ROOT_CBV_COUNT ? desc.bindingCount : ROOT_CBV_COUNT;
	for (u32 i = 0; i < res->bindingCount; ++i) res->bindings[i] = desc.bindings[i];

	return { h };
}

void Backend::descriptor_set_update(DescriptorSetHandle set, const DescriptorSetDesc &desc)
{
	DescriptorSetResource *res = g_descriptorSetPool.get(set.handle);
	if (!res)
	{
		TerminalDebug::println(PrintColorType_Red, "descriptor_set_update: invalid handle");
		return;
	}
	res->bindingCount = desc.bindingCount < ROOT_CBV_COUNT ? desc.bindingCount : ROOT_CBV_COUNT;
	for (u32 i = 0; i < res->bindingCount; ++i) res->bindings[i] = desc.bindings[i];
}

void Backend::descriptor_set_destroy(DescriptorSetHandle set)
{
	g_descriptorSetPool.release(set.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command list execution — translates recorded Commands into native D3D12 calls
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Backend::command_list_execute(::CommandList *cmdList)
{
	PipelineResource *currentPipeline = nullptr;

	for (u32 i = 0; i < cmdList->commandCount; ++i)
	{
		const Command &cmd = cmdList->commands[i];

		switch (cmd.type)
		{
			case CommandType::SetPipeline:
			{
				currentPipeline = g_pipelinePool.get(cmd.setPipeline.pipeline.handle);
				if (!currentPipeline)
				{
					TerminalDebug::println(PrintColorType_Red, "SetPipeline: invalid handle");
					break;
				}
				commandList->SetPipelineState(currentPipeline->pso);
				commandList->IASetPrimitiveTopology(currentPipeline->topology);
				break;
			}

			case CommandType::SetDescriptorSet:
			{
				DescriptorSetResource *set = g_descriptorSetPool.get(cmd.setDescriptorSet.set.handle);
				if (!set)
				{
					TerminalDebug::println(PrintColorType_Red, "SetDescriptorSet: invalid handle");
					break;
				}
				for (u32 b = 0; b < set->bindingCount; ++b)
				{
					const DescriptorBinding &binding = set->bindings[b];
					if (binding.type == DescriptorType::UniformBuffer)
					{
						BufferResource *buf = g_bufferPool.get(binding.buffer.handle);
						if (buf) commandList->SetGraphicsRootConstantBufferView(binding.slot, buf->resource->GetGPUVirtualAddress());
					}
					else if (binding.type == DescriptorType::Texture)
					{
						TextureResource *tex = g_texturePool.get(binding.texture.handle);
						if (tex)
						{
							// Fixed slot t0 today (see ROOT_PARAM_TEXTURE_TABLE) — binding.slot is
							// ignored for textures until multiple simultaneous textures are needed.
							D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
							gpuHandle.ptr += (UINT64)binding.texture.handle.index * srvDescriptorSize;
							commandList->SetGraphicsRootDescriptorTable(ROOT_PARAM_TEXTURE_TABLE, gpuHandle);
						}
					}
					else // DescriptorType::Sampler
					{
						// No-op: the one sampler this backend supports is a STATIC sampler baked
						// into the root signature (s0, linear-wrap) at init time — see Backend::init.
						// gpu.hpp has no SamplerHandle to bind at runtime, so there's nothing to do
						// here; this branch exists so a Sampler binding doesn't hit the old TODO.
					}
				}
				break;
			}

			case CommandType::DrawMesh:
			{
				if (!currentPipeline)
				{
					TerminalDebug::println(PrintColorType_Red, "DrawMesh with no pipeline bound");
					break;
				}

				const MeshDesc *mesh = Gpu::mesh_get(cmd.drawMesh.mesh);
				if (!mesh)
				{
					TerminalDebug::println(PrintColorType_Red, "DrawMesh: invalid mesh handle");
					break;
				}

				BufferResource *vb = g_bufferPool.get(mesh->vertexBuffer.handle);
				if (!vb)
				{
					TerminalDebug::println(PrintColorType_Red, "DrawMesh: invalid vertex buffer");
					break;
				}

				D3D12_VERTEX_BUFFER_VIEW vbView = {};
				vbView.BufferLocation = vb->resource->GetGPUVirtualAddress();
				vbView.StrideInBytes = currentPipeline->vertexStride;
				vbView.SizeInBytes = vb->size;
				commandList->IASetVertexBuffers(0, 1, &vbView);

				if (mesh->indexType != IndexType::None)
				{
					BufferResource *ib = g_bufferPool.get(mesh->indexBuffer.handle);
					if (!ib)
					{
						TerminalDebug::println(PrintColorType_Red, "DrawMesh: invalid index buffer");
						break;
					}

					D3D12_INDEX_BUFFER_VIEW ibView = {};
					ibView.BufferLocation = ib->resource->GetGPUVirtualAddress();
					ibView.SizeInBytes = ib->size;
					ibView.Format = (mesh->indexType == IndexType::U16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
					commandList->IASetIndexBuffer(&ibView);

					commandList->DrawIndexedInstanced(mesh->indexCount, cmd.drawMesh.instanceCount, 0, 0, 0);
				}
				else
				{
					commandList->DrawInstanced(mesh->vertexCount, cmd.drawMesh.instanceCount, 0, 0);
				}
				break;
			}

			case CommandType::Dispatch:
			{
				// TODO: compute needs its own PSO type (D3D12_COMPUTE_PIPELINE_STATE_DESC) —
				// pipeline_create above only builds graphics PSOs so far.
				todo( "Compute dispatch not implemented yet" );
				break;
			}
		}
	}
}

#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////