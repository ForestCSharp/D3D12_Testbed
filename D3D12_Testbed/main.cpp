#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <iostream>
#include <array>
using std::array;

#include "D3D12MemAlloc/D3D12MemAlloc.h"

using Microsoft::WRL::ComPtr;
using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;

struct SceneConstantBuffer
{
	XMFLOAT4 offset;
};

static const UINT frame_count = 3;

#define HR_CHECK(expr)  \
{\
	HRESULT result = (expr);\
	if (FAILED(result))\
	{\
		std::cout << "FAILED HRESULT. line: " << __LINE__ << " code: " << #expr << " error: " << std::hex << result << std::endl;\
		exit(-1);\
	}\
}\

/*  Almost identical to CD3DX12_RESOURCE_BARRIER::Transition in d3dx12.h
	(helper file provided by microsoft that we've opted to not use) */
static D3D12_RESOURCE_BARRIER transition_resource(
	_In_ ID3D12Resource* in_resource,
	const D3D12_RESOURCE_STATES state_before,
	const D3D12_RESOURCE_STATES state_after,
	const UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	const D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
	D3D12_RESOURCE_BARRIER result = {};
	result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	result.Flags = flags;
	result.Transition.pResource = in_resource;
	result.Transition.StateBefore = state_before;
	result.Transition.StateAfter = state_after;
	result.Transition.Subresource = subresource;
	return result;
}

void wait_gpu_idle(ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandQueue> command_queue)
{
	ComPtr<ID3D12Fence> fence;
	HR_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

	const HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fence_event);
	
	HR_CHECK(command_queue->Signal(fence.Get(),1));
	HR_CHECK(fence->SetEventOnCompletion(1, fence_event));
	WaitForSingleObject(fence_event, INFINITE);
}

bool is_key_down(const int in_key)
{
	return GetKeyState(in_key) & 0x8000;
}

bool should_close = false;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CLOSE) should_close = true;
	return DefWindowProc(window, message, wParam, lParam);
}

int main()
{	
	// 1. Create Our Window
	HINSTANCE h_instance = GetModuleHandle(nullptr);

	WNDCLASSEX window_class = { 0 };
	window_class.cbSize = sizeof(WNDCLASSEX);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = h_instance;
	window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
	window_class.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&window_class);

	LONG width = 1280;
	LONG height = 720;
	RECT window_rect = { 0, 0, width, height};
	AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND window = CreateWindow(
		window_class.lpszClassName,
		L"D3D12_Testbed",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_rect.right - window_rect.left,
		window_rect.bottom - window_rect.top,
		nullptr, // We have no parent window.
		nullptr, // We aren't using menus.
		h_instance,
		NULL); //Could pass userdata to window proc here

	ShowWindow(window, SW_SHOW);

	ComPtr<ID3D12Debug> debug_controller;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
	{
		debug_controller->EnableDebugLayer();
	}

	// 2. Create a D3D12 Factory, Adapter, and Device
	UINT dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
	ComPtr<IDXGIFactory4> factory;
	HR_CHECK(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)));

	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<ID3D12Device> device;
	for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapter_index, &adapter); ++adapter_index)
	{
		DXGI_ADAPTER_DESC1 desc = {0};
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, and create device if so
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
		{
			HR_CHECK(D3D12CreateDevice(
				adapter.Get(),
				D3D_FEATURE_LEVEL_12_0,
				IID_PPV_ARGS(&device)
			));
			break;
		}
	}

	D3D12MA::ALLOCATOR_DESC allocator_desc = {};
	allocator_desc.pDevice = device.Get();
	allocator_desc.pAdapter = adapter.Get();

	D3D12MA::Allocator* gpu_memory_allocator = nullptr;
	HR_CHECK(D3D12MA::CreateAllocator(&allocator_desc, &gpu_memory_allocator));

	HANDLE fence_event;
	ComPtr<ID3D12Fence> fence;

	UINT64 fence_values[frame_count];
	memset(fence_values, 0, sizeof(UINT64) * frame_count);

	// 3. Create a command queue
	ComPtr<ID3D12CommandQueue> command_queue;
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HR_CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));

	// 4. Create the swapchain and associate it with our window
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.BufferCount = frame_count;
	swap_chain_desc.Width = width;
	swap_chain_desc.Height = height;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapchain_1;
	HR_CHECK(factory->CreateSwapChainForHwnd(
		command_queue.Get(), // Swap chain needs the queue so that it can force a flush on it.
		window,
		&swap_chain_desc,
		nullptr,
		nullptr,
		&swapchain_1
	));

	HR_CHECK(factory->MakeWindowAssociation(window, 0));
	
	ComPtr<IDXGISwapChain3> swapchain;
	HR_CHECK(swapchain_1.As(&swapchain));
	
	UINT frame_index = swapchain->GetCurrentBackBufferIndex();

	// 5. Create descriptor heaps (1 per frame)
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.NumDescriptors = frame_count;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap;
	HR_CHECK(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)));

	UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	array<ComPtr<ID3D12DescriptorHeap>, frame_count> cbv_descriptor_heaps;
	for (uint32_t i = 0; i < frame_count; ++i)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = {};
		desc_heap_desc.NumDescriptors = 1;
		desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HR_CHECK(device->CreateDescriptorHeap(&desc_heap_desc, IID_PPV_ARGS(&cbv_descriptor_heaps[i])));
	}

	array<ComPtr<ID3D12CommandAllocator>, frame_count> command_allocators;
	array<ComPtr<ID3D12Resource>, frame_count> render_targets;
	// 6. Create render target views (1 per frame), associating them with corresponding rtv descriptor heaps
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle(rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

		// Create a render target view for each frame.
		for (UINT current_frame_index = 0; current_frame_index < frame_count; current_frame_index++)
		{
			HR_CHECK(swapchain->GetBuffer(current_frame_index, IID_PPV_ARGS(&render_targets[current_frame_index])));

			D3D12_RENDER_TARGET_VIEW_DESC render_target_view_desc = {};
			render_target_view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			render_target_view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

			device->CreateRenderTargetView(render_targets[current_frame_index].Get(), &render_target_view_desc, rtv_descriptor_handle);
			rtv_descriptor_handle.ptr += rtv_heap_offset;

			// 7. Create a command allocator per-frame, which will be used to create our command lists
			HR_CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[current_frame_index])));
		}
	}


	// 8. Create our (empty) root signature, which describes resources to be used when running work on the GPU
	ComPtr<ID3D12RootSignature> root_signature;
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		range.NumDescriptors = 1;
		range.BaseShaderRegister = 0;
		range.RegisterSpace = 0;
		range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 root_parameter = {};
		root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameter.DescriptorTable.NumDescriptorRanges = 1;
		root_parameter.DescriptorTable.pDescriptorRanges = &range;
		root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
		root_signature_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		root_signature_desc.Desc_1_1 = {
			1,
			&root_parameter,
			0,
			nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
		};

		ComPtr<ID3DBlob> signature_blob, error_blob;
		HR_CHECK(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature_blob, &error_blob));
		HR_CHECK(device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	// 9. Compile our vertex and pixel shaders
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	ComPtr<ID3DBlob> vertex_shader, pixel_shader;
	HR_CHECK(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertex_shader, nullptr));
	HR_CHECK(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixel_shader, nullptr));

	// 10. Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC input_element_descs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 11. Create Graphics Pipeline State Object
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};

	// 11. A. Set Our Root signature, input_elements, and shaders
	pso_desc.pRootSignature = root_signature.Get();
	pso_desc.InputLayout = { input_element_descs, _countof(input_element_descs) };
	pso_desc.VS = { vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize() };
	pso_desc.PS = { pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize() };

	// 11. B. Setup The Rasterizer
	pso_desc.RasterizerState = {};
	pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
	pso_desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	pso_desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	pso_desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	pso_desc.RasterizerState.DepthClipEnable = TRUE;
	pso_desc.RasterizerState.MultisampleEnable = FALSE;
	pso_desc.RasterizerState.AntialiasedLineEnable = FALSE;
	pso_desc.RasterizerState.ForcedSampleCount = 0;
	pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	// 11. C. Setup Blend State
	pso_desc.BlendState = {};
	pso_desc.BlendState.AlphaToCoverageEnable = FALSE;
	pso_desc.BlendState.IndependentBlendEnable = FALSE;
	for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
	{
		pso_desc.BlendState.RenderTarget[i] =
		{
			FALSE,FALSE,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};
	}

	// 11. D. Disable Depth,Stencil Tests, Set Primitive Topology, describe render target formats
	pso_desc.DepthStencilState.DepthEnable = FALSE;
	pso_desc.DepthStencilState.StencilEnable = FALSE;
	pso_desc.SampleMask = UINT_MAX;
	pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso_desc.NumRenderTargets = 1;
	pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pso_desc.SampleDesc.Count = 1;

	ComPtr<ID3D12PipelineState> pipeline_state;
	HR_CHECK(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));

	// 12. Create Command list using command allocator and pipeline state, and close it (we'll record it later)
	//FCS TODO: one command_list per frame_index, do this in while loop!
	ComPtr<ID3D12GraphicsCommandList> command_list;
	HR_CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[frame_index].Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	HR_CHECK(command_list->Close());

	// 13. Create The Vertex Buffer, copy vertices into it
	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	Vertex triangle_vertices[] =
	{
		{ { 0.0f, 0.25f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ { 0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
		{ { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
	};

	const UINT vertex_buffer_size = sizeof(triangle_vertices);

	// Note: using upload heaps to transfer static data like vert buffers is not 
	// recommended. Every time the GPU needs it, the upload heap will be marshalled 
	// over. Please read up on Default Heap usage. An upload heap is used here for 
	// code simplicity and because there are very few verts to actually transfer.
	D3D12_HEAP_PROPERTIES upload_heap_properties = {};
	upload_heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
	upload_heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	upload_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	upload_heap_properties.CreationNodeMask = 1;
	upload_heap_properties.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Alignment = 0;
	resource_desc.Width = vertex_buffer_size;
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.SampleDesc.Quality = 0;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// Note: using upload heaps to transfer static data like vert buffers is not 
	// recommended. Every time the GPU needs it, the upload heap will be marshalled 
	// over. Please read up on Default Heap usage. An upload heap is used here for 
	// code simplicity and because there are very few verts to actually transfer.
	D3D12MA::ALLOCATION_DESC alloc_desc = {};
	alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

	ComPtr<ID3D12Resource> vertex_buffer;
	D3D12MA::Allocation* vertex_buffer_allocation = nullptr;
	HR_CHECK(gpu_memory_allocator->CreateResource(
		&alloc_desc,
		&resource_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		&vertex_buffer_allocation,
		IID_PPV_ARGS(&vertex_buffer)
	));

	// Copy the triangle data to the vertex buffer.
	UINT8* pVertexDataBegin;
	D3D12_RANGE read_range = { 0, 0 }; // We do not intend to read from this resource on the CPU.
	HR_CHECK(vertex_buffer->Map(0, &read_range, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, triangle_vertices, sizeof(triangle_vertices));
	vertex_buffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view.
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
	vertex_buffer_view.StrideInBytes = sizeof(Vertex);
	vertex_buffer_view.SizeInBytes = vertex_buffer_size;

	// 14. Create Constant Buffer
	resource_desc.Width = (sizeof(SceneConstantBuffer) + 255) & ~255;

	array<ComPtr<ID3D12Resource>,frame_count> constant_buffers;
	D3D12MA::Allocation* constant_buffer_allocations[frame_count];
	UINT8* cbv_gpu_addresses[frame_count];

	for (uint32_t i = 0; i < frame_count; ++i)
	{
		ComPtr<ID3D12Resource>& constant_buffer = constant_buffers[i];

		HR_CHECK(gpu_memory_allocator->CreateResource(
			&alloc_desc,
			&resource_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			&constant_buffer_allocations[i],
			IID_PPV_ARGS(&constant_buffer)
		));

		// Describe and create a constant buffer view.
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		cbv_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
		cbv_desc.SizeInBytes = static_cast<UINT>(resource_desc.Width);
		device->CreateConstantBufferView(&cbv_desc, cbv_descriptor_heaps[i]->GetCPUDescriptorHandleForHeapStart());

		// Map and initialize the constant buffer. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		HR_CHECK(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&cbv_gpu_addresses[i])));
	}


	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		for (uint32_t i = 0; i < frame_count; ++i)
		{
			fence_values[i] = 0;
		}

		HR_CHECK(device->CreateFence(fence_values[frame_index], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		fence_values[frame_index]++;

		// Create an event handle to use for frame synchronization.
		fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (fence_event == nullptr)
		{
			HR_CHECK(HRESULT_FROM_WIN32(GetLastError()));
		}

		{   //WaitForGPU...
			// Schedule a Signal command in the queue.
			HR_CHECK(command_queue->Signal(fence.Get(), fence_values[frame_index]));

			// Wait until the fence has been processed.
			HR_CHECK(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
			WaitForSingleObjectEx(fence_event, INFINITE, FALSE);

			// Increment the fence value for the current frame.
			fence_values[frame_index]++;
		}
	}

	SceneConstantBuffer constant_buffer_data = {};

	while (!should_close)
	{
		if (is_key_down(VK_ESCAPE))
		{
			should_close = true;
		}
		
		// Process any messages in the queue.
		MSG msg = {};
		while (PeekMessage(&msg, window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		{ //Rendering
			const float translation_speed = 0.005f;
			const float offset_bounds = 1.25f;

			constant_buffer_data.offset.x += translation_speed;
			if (constant_buffer_data.offset.x > offset_bounds)
			{
				constant_buffer_data.offset.x = -offset_bounds;
			}
			memcpy(cbv_gpu_addresses[frame_index], &constant_buffer_data, sizeof(constant_buffer_data));

			HR_CHECK(command_allocators[frame_index]->Reset());

			HR_CHECK(command_list->Reset(command_allocators[frame_index].Get(), pipeline_state.Get()));

			// Set necessary state.
			command_list->SetGraphicsRootSignature(root_signature.Get());

			ID3D12DescriptorHeap* ppHeaps[] = { cbv_descriptor_heaps[frame_index].Get() };
			command_list->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

			command_list->SetGraphicsRootDescriptorTable(0, cbv_descriptor_heaps[frame_index]->GetGPUDescriptorHandleForHeapStart());

			D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
			command_list->RSSetViewports(1, &viewport);

			D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			command_list->RSSetScissorRects(1, &scissor_rect);

			// Indicate that the back buffer will be used as a render target.
			D3D12_RESOURCE_BARRIER present_to_rt_barrier = transition_resource(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			command_list->ResourceBarrier(1, &present_to_rt_barrier);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			rtv_handle.ptr += frame_index * rtv_heap_offset;
			command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
			
			// Record commands.
			const float clear_color[] = { 0.0f, 0.1f, 0.2f, 1.0f };
			command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
			command_list->DrawInstanced(3, 1, 0, 0);

			// Indicate that the back buffer will now be used to present.
			D3D12_RESOURCE_BARRIER rt_to_present_barrier = transition_resource(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			command_list->ResourceBarrier(1, &rt_to_present_barrier);

			HR_CHECK(command_list->Close());

			// Execute the command list.
			ID3D12CommandList* ppCommandLists[] = { command_list.Get() };
			command_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			// Present the frame.
			HR_CHECK(swapchain->Present(1, 0));

			{   // WaitForPreviousFrame...
				// Signal and increment the fence value.
				const UINT64 current_fence_value = fence_values[frame_index];
				HR_CHECK(command_queue->Signal(fence.Get(), current_fence_value));

				frame_index = swapchain->GetCurrentBackBufferIndex();
				// Wait until the previous frame is finished.
				if (fence->GetCompletedValue() < fence_values[frame_index])
				{
					HR_CHECK(fence->SetEventOnCompletion(fence_values[frame_index], fence_event));
					WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
				}

				fence_values[frame_index] = current_fence_value + 1;
			}
		}
	}

	wait_gpu_idle(device, command_queue);

	
	{ //Free all memory allocated with D3D12 Memory Allocator
		vertex_buffer_allocation->Release();

		for (UINT i = 0; i < frame_count; ++i)
		{
			constant_buffer_allocations[i]->Release();
		}
	}

	gpu_memory_allocator->Release();
	
	return 0;
}