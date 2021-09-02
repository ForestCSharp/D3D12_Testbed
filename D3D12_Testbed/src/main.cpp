#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

//Windows Libs
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

//Std Lib
#include <vector>
#include <array>
using std::array;

//C
#include "time.h"
#include "math.h"

//GLB Loader
#include "gltf.h"

//D3D12 Helpers
#include "D3D12MemAlloc/D3D12MemAlloc.h"
#include "d3dx12.h"


#include "d3d12_helpers.h"
#include "d3d12_texture.h"

#define IMGUI_IMPLEMENTATION
#include "../third_party/DearImGui/misc/single_file/imgui_single_file.h"

#include "../third_party/DearImGui/backends/imgui_impl_win32.h"
#include "../third_party/DearImGui/backends/imgui_impl_dx12.h"

struct SceneConstantBuffer
{
	XMMATRIX view;
	XMMATRIX proj;
	XMVECTOR cam_pos;
	XMVECTOR cam_dir;
};

struct InstanceConstantBuffer
{
	UINT texture_index = 0;
	UINT texture_lod = 0;
};

struct SpecularPrefilterConstantBuffer
{
	UINT texture_index;
	float roughness;
};

struct MeshRenderConstantBuffer
{
	UINT diffuse_ibl_texture_index;
	UINT specular_ibl_texture_index;
	UINT specular_ibl_mip_count;
	UINT specular_lut_texture_index;
};

struct TextureViewerData
{
	UINT texture_index = 0;
	UINT texture_lod = 0;
};

//TODO: Move to helpers file
//Templated constant buffer
template <typename T>
struct TConstantBuffer
{
	ComPtr<ID3D12Resource> resource;
	D3D12MA::Allocation* allocation = nullptr;
	T* data_ptr = nullptr;

	TConstantBuffer() = default; //TODO: for std::array in TConstantBufferArray
	
	explicit TConstantBuffer(D3D12MA::Allocator* gpu_memory_allocator)
	{
		D3D12MA::ALLOCATION_DESC alloc_desc = {};
		alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
 
		D3D12_RESOURCE_DESC resource_desc = {};
		resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource_desc.Alignment = 0;
		resource_desc.Width = (sizeof(T)+ 255) & ~255;
		resource_desc.Height = 1;
		resource_desc.DepthOrArraySize = 1;
		resource_desc.MipLevels = 1;
		resource_desc.Format = DXGI_FORMAT_UNKNOWN;
		resource_desc.SampleDesc.Count = 1;
		resource_desc.SampleDesc.Quality = 0;
		resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
 
		HR_CHECK(gpu_memory_allocator->CreateResource(
            &alloc_desc,
            &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            &allocation,
            IID_PPV_ARGS(&resource)
        ));
 
		HR_CHECK(resource->Map(0, &no_read_range, reinterpret_cast<void**>(&data_ptr)));
	}

	T& data()
	{
		assert(data_ptr != nullptr);
		return *data_ptr;
	}

	D3D12_GPU_VIRTUAL_ADDRESS get_gpu_virtual_address() const
	{
		return resource->GetGPUVirtualAddress();
	}

	void release() const
	{
		if (allocation)
		{
			allocation->Release();
		}
	}
};

template <typename T, size_t Count>
struct TConstantBufferArray
{
	array<TConstantBuffer<T>, Count> constant_buffers;
	
	explicit TConstantBufferArray(D3D12MA::Allocator* gpu_memory_allocator)
	{
		for (size_t i = 0; i < Count; ++i)
		{
			constant_buffers[i] = TConstantBuffer<T>(gpu_memory_allocator);
		}
	}

	T& data(size_t index)
	{
		assert(index < constant_buffers.size());
		return constant_buffers[index].data();
	}

	size_t count() const
	{
		return constant_buffers.size();
	}

	D3D12_GPU_VIRTUAL_ADDRESS get_gpu_virtual_address(size_t index) const
	{
		assert(index < constant_buffers.size());
		return constant_buffers[index].get_gpu_virtual_address();
	}

	void release() const
	{
		for (size_t i = 0; i < Count; ++i)
		{
			constant_buffers[i].release();
		}
	}
};

struct GpuVertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT4 color;
	XMFLOAT2 uv;
};

static const UINT backbuffer_count = 3;

bool is_key_down(const int in_key)
{
	return GetKeyState(in_key) & 0x8000;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

int main()
{	
	// Create Our Window
	HINSTANCE h_instance = GetModuleHandle(nullptr);

	WNDCLASSEX window_class = { 0 };
	window_class.cbSize = sizeof(WNDCLASSEX);
	window_class.style = CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc = WndProc;
	window_class.hInstance = h_instance;
	window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
	window_class.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&window_class);

	bool borderless_fullscreen = false;
	
	auto window_style = borderless_fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
	LONG width  = borderless_fullscreen ? GetSystemMetrics(SM_CXSCREEN) : 1280;
	LONG height = borderless_fullscreen ? GetSystemMetrics(SM_CYSCREEN) : 720;
	RECT window_rect = { 0, 0, width, height};
	AdjustWindowRect(&window_rect, window_style, FALSE);

	HWND window = CreateWindow(
		window_class.lpszClassName,
		L"D3D12 Testbed",
		window_style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_rect.right - window_rect.left,
		window_rect.bottom - window_rect.top,
		nullptr, // no parent window
		nullptr, // no menus
		h_instance,
		NULL); //Could pass userdata to window proc here

	ShowWindow(window, SW_SHOW);

	if (IsDebuggerPresent())
	{
		ComPtr<ID3D12Debug> debug_controller;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
		{
			debug_controller->EnableDebugLayer();
		}
	}

	// 2. Create a D3D12 Factory, Adapter, and Device
	UINT dxgi_factory_flags	 = DXGI_CREATE_FACTORY_DEBUG;
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
			HR_CHECK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
			break;
		}
	}

	D3D12MA::ALLOCATOR_DESC allocator_desc = {};
	allocator_desc.pDevice = device.Get();
	allocator_desc.pAdapter = adapter.Get();

	D3D12MA::Allocator* gpu_memory_allocator = nullptr;
	HR_CHECK(D3D12MA::CreateAllocator(&allocator_desc, &gpu_memory_allocator));

	// 3. Create a command queue
	ComPtr<ID3D12CommandQueue> command_queue;
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HR_CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));

	HR_CHECK(factory->MakeWindowAssociation(window, 0));

	array<ComPtr<ID3D12CommandAllocator>, backbuffer_count> command_allocators;
	for (UINT current_frame_index = 0; current_frame_index < backbuffer_count; current_frame_index++)
	{
		// Create a command allocator per-frame, which will be used to create our command lists
		HR_CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[current_frame_index])));
	}

	//Init ImGui
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui_ImplWin32_Init(window);

	D3D12_DESCRIPTOR_HEAP_DESC imgui_descriptor_heap_desc = {};
	imgui_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imgui_descriptor_heap_desc.NumDescriptors = 1;
	imgui_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ComPtr<ID3D12DescriptorHeap> imgui_descriptor_heap;
	HR_CHECK(device->CreateDescriptorHeap(&imgui_descriptor_heap_desc, IID_PPV_ARGS(&imgui_descriptor_heap)));

	ImGui_ImplDX12_Init(device.Get(),
						backbuffer_count, 
						DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 
						imgui_descriptor_heap.Get(),
						imgui_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), 
						imgui_descriptor_heap->GetGPUDescriptorHandleForHeapStart()
					   );

	struct FrameResources
	{
		ComPtr<IDXGISwapChain3> swapchain;

		ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap;
		array<ComPtr<ID3D12Resource>, backbuffer_count> render_targets;
		
		ComPtr<ID3D12Resource> depth_texture;
		D3D12MA::Allocation* depth_texture_allocation = nullptr;
		ComPtr<ID3D12DescriptorHeap> depth_descriptor_heap;

		//Synchronization
		UINT frame_index = 0;
		UINT64 fence_values[backbuffer_count];
		ComPtr<ID3D12Fence> fence;
		HANDLE fence_event = INVALID_HANDLE_VALUE;

		//TODO: Need a struct to hold device, command_queue, factory, etc.
		FrameResources(const UINT in_width, const UINT in_height, ComPtr<IDXGIFactory4> factory, ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const ComPtr<ID3D12CommandQueue> command_queue, const HWND window)
		{
			DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
			swap_chain_desc.BufferCount = backbuffer_count;
			swap_chain_desc.Width = in_width;
			swap_chain_desc.Height = in_height;
			swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swap_chain_desc.SampleDesc.Count = 1;
			swap_chain_desc.Flags = 0;

			ComPtr<IDXGISwapChain1> swapchain_1;
			HR_CHECK(factory->CreateSwapChainForHwnd(
                command_queue.Get(), // Swap chain needs the queue so that it can force a flush on it.
                window,
                &swap_chain_desc,
                nullptr,
                nullptr,
                &swapchain_1
            ));
	
			HR_CHECK(swapchain_1.As(&swapchain));

			memset(fence_values, 0, sizeof(UINT64) * backbuffer_count);

			HR_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

			fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (fence_event == nullptr)
			{
				HR_CHECK(HRESULT_FROM_WIN32(GetLastError()));
			}

			resize(in_width, in_height, device, command_queue, gpu_memory_allocator);
		}

		void resize(const UINT in_width, const UINT in_height, ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandQueue> command_queue, D3D12MA::Allocator* gpu_memory_allocator)
		{
			wait_gpu_idle(device, command_queue);

			if (swapchain.Get() != nullptr)
			{			
				for (auto& render_target : render_targets)
				{
					render_target.Reset();
				}

				rtv_descriptor_heap.Reset();
				
				swapchain->ResizeBuffers(backbuffer_count, in_width, in_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
			}

			D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
			rtv_heap_desc.NumDescriptors = backbuffer_count;
			rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			HR_CHECK(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)));
			rtv_descriptor_heap->SetName(TEXT("rtv_descriptor_heap"));
			

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle(rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

			// Create a render target view for each frame.
			for (UINT current_frame_index = 0; current_frame_index < backbuffer_count; current_frame_index++)
			{
				HR_CHECK(swapchain->GetBuffer(current_frame_index, IID_PPV_ARGS(&render_targets[current_frame_index])));

				D3D12_RENDER_TARGET_VIEW_DESC render_target_view_desc = {};
				render_target_view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				render_target_view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

				device->CreateRenderTargetView(render_targets[current_frame_index].Get(), &render_target_view_desc, rtv_descriptor_handle);
				const UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				rtv_descriptor_handle.ptr += rtv_heap_offset;
			}

			if (depth_texture_allocation != nullptr)
			{
				depth_texture_allocation->Release();
			}

			if (depth_texture.Get() != nullptr)
			{
				depth_texture.Reset();
			}

			D3D12MA::ALLOCATION_DESC depth_alloc_desc = {};
			depth_alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

			D3D12_RESOURCE_DESC depth_resource_desc = {};
			depth_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			depth_resource_desc.Alignment = 0;
			depth_resource_desc.Width = in_width;
			depth_resource_desc.Height = in_height;
			depth_resource_desc.DepthOrArraySize = 1;
			depth_resource_desc.MipLevels = 1;
			depth_resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
			depth_resource_desc.SampleDesc.Count = 1;
			depth_resource_desc.SampleDesc.Quality = 0;
			depth_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			depth_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE depth_clear_value = {};
			depth_clear_value.Format = DXGI_FORMAT_D32_FLOAT;
			depth_clear_value.DepthStencil.Depth = 1.0f;
			depth_clear_value.DepthStencil.Stencil = 0;

			gpu_memory_allocator->CreateResource(
	            &depth_alloc_desc,
	            &depth_resource_desc,
	            D3D12_RESOURCE_STATE_DEPTH_WRITE,
	            &depth_clear_value,
	            &depth_texture_allocation,
	            IID_PPV_ARGS(&depth_texture)
	        );

			D3D12_DESCRIPTOR_HEAP_DESC depth_heap_desc = {};
			depth_heap_desc.NumDescriptors = 1;
			depth_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			depth_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			HR_CHECK(device->CreateDescriptorHeap(&depth_heap_desc, IID_PPV_ARGS(&depth_descriptor_heap)));
			depth_descriptor_heap->SetName(TEXT("depth_descriptor_heap"));

			D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
			dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
			dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

			device->CreateDepthStencilView(depth_texture.Get(), &dsv_desc, depth_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

			for (size_t i = 0; i < backbuffer_count; ++i)
			{
				fence_values[i] = fence_values[frame_index];
			}
			
			frame_index = swapchain->GetCurrentBackBufferIndex();
		}

		void wait_for_previous_frame(ComPtr<ID3D12CommandQueue> command_queue)
		{			
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
	};

	FrameResources frame_resources(width, height, factory, device, gpu_memory_allocator, command_queue, window);

	//TODO: Helpers for this in BindlessResourceManager?
	ComPtr<ID3D12RootSignature> bindless_root_signature;
	{
		array<CD3DX12_ROOT_PARAMETER, 4> root_parameters;

		// Constant Buffer View
		root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		
		root_parameters[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);

		// Bindless resources (texture2Ds)
		D3D12_DESCRIPTOR_RANGE texture_range = {};
		texture_range.BaseShaderRegister = 0;
		texture_range.NumDescriptors = BINDLESS_TABLE_SIZE;
		texture_range.OffsetInDescriptorsFromTableStart = 0;
		texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		texture_range.RegisterSpace = TEXTURE_2D_REGISTER_SPACE;
		root_parameters[2].InitAsDescriptorTable(1, &texture_range);

		// Bindless Resources (cubemaps)
		D3D12_DESCRIPTOR_RANGE cube_range = {};
		cube_range.BaseShaderRegister = 0;
		cube_range.NumDescriptors = BINDLESS_TABLE_SIZE;
		cube_range.OffsetInDescriptorsFromTableStart = 0;
		cube_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		cube_range.RegisterSpace = TEXTURE_CUBE_REGISTER_SPACE;
		root_parameters[3].InitAsDescriptorTable(1, &cube_range);

		std::array<CD3DX12_STATIC_SAMPLER_DESC, 1> samplers;
		samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
		samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_0(static_cast<UINT>(root_parameters.size()), root_parameters.data(),
            static_cast<UINT>(samplers.size()), samplers.data(),
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature_blob, error_blob;
		HR_CHECK(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature_blob, &error_blob));
		HR_CHECK(device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&bindless_root_signature)));
	}

	ComPtr<ID3D12PipelineState> pbr_pipeline_state = GraphicsPipelineBuilder()
		.with_root_signature(bindless_root_signature)
		.with_vs(compile_shader(L"data/shaders/pbr.hlsl", "vs_main", "vs_5_1"))
		.with_ps(compile_shader(L"data/shaders/pbr.hlsl", "ps_main", "ps_5_1"))
		.with_depth_enabled(true)
		.with_dsv_format(DXGI_FORMAT_D32_FLOAT)
		.with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
		.with_rtv_formats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})
		.with_debug_name(L"pipeline_state")
		.build(device);

	ComPtr<ID3D12PipelineState> skybox_pipeline_state = GraphicsPipelineBuilder()
        .with_root_signature(bindless_root_signature)
        .with_vs(compile_shader(L"data/shaders/skybox.hlsl", "vs_main", "vs_5_1"))
        .with_ps(compile_shader(L"data/shaders/skybox.hlsl", "ps_main", "ps_5_1"))
        .with_depth_enabled(true)
        .with_dsv_format(DXGI_FORMAT_D32_FLOAT)
        .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .with_rtv_formats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})
		.with_cull_mode(D3D12_CULL_MODE_NONE)
		.with_debug_name(L"skybox_pipeline_state")
        .build(device);

	ComPtr<ID3D12PipelineState> texture_viewer_pipeline_state = GraphicsPipelineBuilder()
	    .with_root_signature(bindless_root_signature)
	    .with_vs(compile_shader(L"data/shaders/texture_viewer.hlsl", "vs_main", "vs_5_1"))
	    .with_ps(compile_shader(L"data/shaders/texture_viewer.hlsl", "ps_main", "ps_5_1"))
	    .with_depth_enabled(false)
	    .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
	    .with_rtv_formats({DXGI_FORMAT_R8G8B8A8_UNORM_SRGB})
	    .with_debug_name(L"texture_viewer_pipeline_state")
	    .build(device);

	// 12. Create Command list using command allocator and pipeline state, and close it (we'll record it later)
	ComPtr<ID3D12GraphicsCommandList> command_list;
	HR_CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[frame_resources.frame_index].Get(), pbr_pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	HR_CHECK(command_list->Close());

	//Load Environment Map
	Texture hdr_equirectangular_texture(device, gpu_memory_allocator, command_queue, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, "data/hdr/Newport_Loft.hdr");
	hdr_equirectangular_texture.set_name("Env Map (equirectangular)");

	const UINT hdr_cube_size = 1024;
	DXGI_FORMAT cubemap_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	Texture hdr_cubemap_texture(device, gpu_memory_allocator, cubemap_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, hdr_cube_size, hdr_cube_size, 6);
	hdr_cubemap_texture.set_name("HDR Cubemap Texture");
	hdr_cubemap_texture.set_is_cubemap(true);

	const UINT ibl_cube_size = 16;
	Texture ibl_cubemap_texture(device, gpu_memory_allocator, cubemap_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, ibl_cube_size, ibl_cube_size, 6);
	ibl_cubemap_texture.set_name("IBL Cubemap Texture");
	ibl_cubemap_texture.set_is_cubemap(true);

	const UINT specular_cube_size = 128;
	const UINT prefilter_mip_levels = 6;
	Texture specular_cubemap_texture(device, gpu_memory_allocator, cubemap_format, prefilter_mip_levels, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, specular_cube_size, specular_cube_size, 6);
	specular_cubemap_texture.set_name("Specular Cubemap Texture");
	specular_cubemap_texture.set_is_cubemap(true);

	const UINT specular_lut_size = 512;
	const DXGI_FORMAT specular_lut_format = DXGI_FORMAT_R16G16_FLOAT;
	Texture specular_lut_texture(device, gpu_memory_allocator, specular_lut_format, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, specular_lut_size, specular_lut_size, 1);
	specular_lut_texture.set_name("Specular LUT Texture");

	Texture reference_lut(device, gpu_memory_allocator, command_queue, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, "data/textures/Reference_Lut.png");
	reference_lut.set_name("REFERENCE LUT");

	BindlessResourceManager bindless_resource_manager(device, gpu_memory_allocator);

	//TODO: cubemap specific register function (checks that texture has 6 array elements), remove set_is_cubemap function from "Texture"
	bindless_resource_manager.register_texture(hdr_cubemap_texture);
	bindless_resource_manager.register_texture(ibl_cubemap_texture);
	bindless_resource_manager.register_texture(specular_cubemap_texture);

	bindless_resource_manager.register_texture(hdr_equirectangular_texture);
	bindless_resource_manager.register_texture(specular_lut_texture);
	bindless_resource_manager.register_texture(reference_lut);

	ID3D12DescriptorHeap* bindless_heaps[] = { bindless_resource_manager.bindless_descriptor_heap.Get()};

	//Setup cube
	//Note: We render all faces at once, so we really only need one "Face" to rasterize (4 verts, 6 indices)
	//	    Then, after its rasterized, we rotate it's world pos to sample the correct portion of the equirectangular map
	std::vector<GpuVertex> cube_vertices =
	{
		{{-1.0, -1.0,  1.0}, {}, {}},
		{{1.0, -1.0,  1.0}, {}, {}},
		{{1.0,  1.0,  1.0}, {}, {}},
		{{-1.0,  1.0,  1.0}, {}, {}},
		{{-1.0, -1.0,  -1.0}, {}, {}},
		{{1.0, -1.0,  -1.0}, {}, {}},
		{{1.0,  1.0,  -1.0}, {}, {}},
		{{-1.0,  1.0,  -1.0}, {}, {}},
	};

	std::vector<UINT32> cube_indices =
	{
		0, 1, 2, 2, 3, 0, // front
        1, 5, 6, 6, 2, 1, // right
        7, 6, 5, 5, 4, 7, // back
        4, 0, 3, 3, 7, 4, // left
        4, 5, 1, 1, 0, 4, // bottom
        3, 2, 6, 6, 7, 3  // top
	};
	
	Mesh cube(gpu_memory_allocator, cube_vertices, cube_indices);

	//Setup Quad
	struct QuadVertex
	{
		XMFLOAT2 position;
		XMFLOAT2 uv;
	};

	std::vector<QuadVertex> quad_vertices =
	{
		{{-1.0f, 1.0f}, {0.0f, 0.0f}},
		{{-1.0f,-1.0f}, {0.0f, 1.0f}},
		{{ 1.0f, 1.0f}, {1.0f, 0.0f}},
		{{ 1.0f,-1.0f}, {1.0f, 1.0f}},
	};

	std::vector<UINT> quad_indices =
	{
		0, 1, 2, 1, 2, 3
    };

	Mesh quad(gpu_memory_allocator, quad_vertices, quad_indices);

	auto render_to_cubemap_rtv_formats = { cubemap_format, cubemap_format, cubemap_format,
																	   cubemap_format, cubemap_format, cubemap_format };

	ComPtr<ID3D12PipelineState> spherical_to_cube_pipeline_state = GraphicsPipelineBuilder()
        .with_root_signature(bindless_root_signature)
        .with_vs(compile_shader(L"data/shaders/render_to_cubemap.hlsl", "vs_main", "vs_5_1"))
        .with_ps(compile_shader(L"data/shaders/render_to_cubemap.hlsl", "ps_main", "ps_5_1"))
        .with_depth_enabled(false)
        .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .with_rtv_formats(render_to_cubemap_rtv_formats)
		.with_debug_name(L"spherical_to_cube_pipeline_state")
        .build(device);

	ComPtr<ID3D12PipelineState> diffuse_convolution_pipeline_state = GraphicsPipelineBuilder()
	    .with_root_signature(bindless_root_signature)
	    .with_vs(compile_shader(L"data/shaders/diffuse_convolution.hlsl", "vs_main", "vs_5_1"))
	    .with_ps(compile_shader(L"data/shaders/diffuse_convolution.hlsl", "ps_main", "ps_5_1"))
	    .with_depth_enabled(false)
	    .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
	    .with_rtv_formats(render_to_cubemap_rtv_formats)
	    .with_debug_name(L"diffuse_convolution_pipeline_state")
	    .build(device);

	ComPtr<ID3D12PipelineState> specular_prefilter_pipeline_state = GraphicsPipelineBuilder()
        .with_root_signature(bindless_root_signature)
        .with_vs(compile_shader(L"data/shaders/specular_prefilter.hlsl", "vs_main", "vs_5_1"))
        .with_ps(compile_shader(L"data/shaders/specular_prefilter.hlsl", "ps_main", "ps_5_1"))
        .with_depth_enabled(false)
        .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .with_rtv_formats(render_to_cubemap_rtv_formats)
        .with_debug_name(L"specular_prefilter_pipeline_state")
        .build(device);

	ComPtr<ID3D12PipelineState> specular_lut_pipeline_state = GraphicsPipelineBuilder()
        .with_root_signature(bindless_root_signature)
        .with_vs(compile_shader(L"data/shaders/brdf_lut.hlsl", "vs_main", "vs_5_1"))
        .with_ps(compile_shader(L"data/shaders/brdf_lut.hlsl", "ps_main", "ps_5_1"))
        .with_depth_enabled(false)
        .with_primitive_topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
        .with_rtv_formats({specular_lut_format})
        .with_debug_name(L"specular_lut_pipeline_state")
        .build(device);

	//TODO: FIXME: ^^^ resulting LUT still off (as compared to reference)

	TConstantBuffer<SceneConstantBuffer> spherical_to_cube_scene(gpu_memory_allocator);
	TConstantBuffer<InstanceConstantBuffer> spherical_to_cube_instance(gpu_memory_allocator);
	TConstantBuffer<InstanceConstantBuffer> diffuse_convolution_instance(gpu_memory_allocator);

	XMVECTOR cube_cam_pos	  = XMVectorSet(0.f, 0.f, 0.f, 1.f);
	XMVECTOR cube_cam_forward = XMVectorSet(0.f, 0.f, -1.f, 0.f);
	XMVECTOR cube_cam_up	  = XMVectorSet(0.f, 1.f, 0.f, 0.f);

	SceneConstantBuffer& spherical_to_cube_cbuffer_data = spherical_to_cube_scene.data();
	spherical_to_cube_cbuffer_data.view = XMMatrixLookAtLH(cube_cam_pos, cube_cam_forward, cube_cam_up);
	spherical_to_cube_cbuffer_data.proj = XMMatrixIdentity();
	
	spherical_to_cube_instance.data().texture_index = hdr_equirectangular_texture.bindless_index;
	diffuse_convolution_instance.data().texture_index = hdr_cubemap_texture.bindless_index;

	
	TConstantBufferArray<SpecularPrefilterConstantBuffer, prefilter_mip_levels> specular_prefilter_instance(gpu_memory_allocator);
	for (size_t mip_index = 0; mip_index < prefilter_mip_levels; ++mip_index)
	{
		auto& data = specular_prefilter_instance.data(mip_index);
		data.texture_index = hdr_cubemap_texture.bindless_index;
		data.roughness = static_cast<float>(mip_index) / static_cast<float>(prefilter_mip_levels - 1);
	}

	//Reset command list using this frame's command allocator
	HR_CHECK(command_allocators[frame_resources.frame_index]->Reset());
	HR_CHECK(command_list->Reset(command_allocators[frame_resources.frame_index].Get(), spherical_to_cube_pipeline_state.Get()));

	//Begin Equirectangular to Cubemap
	{
		auto hdr_cubemap_rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(hdr_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		command_list->ResourceBarrier(1, &hdr_cubemap_rt_barrier);
		command_list->OMSetRenderTargets(static_cast<UINT>(hdr_cubemap_texture.rtv_handles.size()), hdr_cubemap_texture.rtv_handles.data(), FALSE, nullptr);

		command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		command_list->SetGraphicsRootSignature(bindless_root_signature.Get());
	
		command_list->SetDescriptorHeaps(_countof(bindless_heaps), bindless_heaps);

		//Slot 0: scene cbuffer
		command_list->SetGraphicsRootConstantBufferView(0, spherical_to_cube_scene.get_gpu_virtual_address());
		//Slot 1: instance cbuffer
		command_list->SetGraphicsRootConstantBufferView(1, spherical_to_cube_instance.get_gpu_virtual_address());
		//Slot 2: bindless texture table
		command_list->SetGraphicsRootDescriptorTable(2, bindless_resource_manager.get_texture_gpu_handle());
		//Slot 3: bindless cubemap table
		command_list->SetGraphicsRootDescriptorTable(3, bindless_resource_manager.get_cubemap_gpu_handle());

		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(hdr_cube_size);
		viewport.Height = static_cast<float>(hdr_cube_size);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		command_list->RSSetViewports(1, &viewport);

		const D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(hdr_cube_size), static_cast<LONG>(hdr_cube_size) };
		command_list->RSSetScissorRects(1, &scissor_rect);

		command_list->IASetVertexBuffers(0, 1, &cube.vertex_buffer_view);
		command_list->IASetIndexBuffer(&cube.index_buffer_view);
		command_list->DrawIndexedInstanced(cube.index_count(), 1, 0, 0, 0);

		auto hdr_cubemap_pixel_shader_barrier = CD3DX12_RESOURCE_BARRIER::Transition(hdr_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		command_list->ResourceBarrier(1, &hdr_cubemap_pixel_shader_barrier);
	}

	//Begin Convolution
	{
		auto ibl_cubemap_rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(ibl_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		command_list->ResourceBarrier(1, &ibl_cubemap_rt_barrier);
	
		//convolution pipeline
		command_list->SetPipelineState(diffuse_convolution_pipeline_state.Get());

		//Slot 1: convolution instance cbuffer
		command_list->SetGraphicsRootConstantBufferView(1, diffuse_convolution_instance.get_gpu_virtual_address());

		command_list->OMSetRenderTargets(static_cast<UINT>(ibl_cubemap_texture.rtv_handles.size()), ibl_cubemap_texture.rtv_handles.data(), FALSE, nullptr);

		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(ibl_cube_size);
		viewport.Height = static_cast<float>(ibl_cube_size);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		command_list->RSSetViewports(1, &viewport);

		const D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(ibl_cube_size), static_cast<LONG>(ibl_cube_size) };
		command_list->RSSetScissorRects(1, &scissor_rect);

		command_list->IASetVertexBuffers(0, 1, &cube.vertex_buffer_view);
		command_list->IASetIndexBuffer(&cube.index_buffer_view);
		command_list->DrawIndexedInstanced(cube.index_count(), 1, 0, 0, 0);

		auto ibl_pixel_shader_barrier = CD3DX12_RESOURCE_BARRIER::Transition(ibl_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		command_list->ResourceBarrier(1, &ibl_pixel_shader_barrier);
	}

	//Specular Prefilter
	/*
	 *		TODO: Generate + use mips of hdr_cubemap_texture
	 *		"One option is to further increase the sample count, but this won't be enough for all environments. As described
	 *		 by Chetan Jags we can reduce this artifact by (during the pre-filter convolution) not directly sampling the
	 *		 environment map, but sampling a mip level of the environment map based on the integral's PDF and the roughness:
	 */
	{
		auto specular_cubemap_rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(specular_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		command_list->ResourceBarrier(1, &specular_cubemap_rt_barrier);
	
		for (size_t mip_index = 0; mip_index < prefilter_mip_levels; ++mip_index)
		{
			command_list->SetPipelineState(specular_prefilter_pipeline_state.Get());

			//Slot 1: specular prefilter instance cbuffer
			command_list->SetGraphicsRootConstantBufferView(1, specular_prefilter_instance.get_gpu_virtual_address(mip_index));

			command_list->OMSetRenderTargets(static_cast<UINT>(specular_cubemap_texture.per_mip_rtv_handles[mip_index].size()), specular_cubemap_texture.per_mip_rtv_handles[mip_index].data(), FALSE, nullptr);

			const UINT mip_width  = specular_cube_size * powf(0.5f, mip_index);
			const UINT mip_height = specular_cube_size * powf(0.5f, mip_index);

			D3D12_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(mip_width);
			viewport.Height = static_cast<float>(mip_height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			command_list->RSSetViewports(1, &viewport);

			const D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(mip_width), static_cast<LONG>(mip_height) };
			command_list->RSSetScissorRects(1, &scissor_rect);

			command_list->IASetVertexBuffers(0, 1, &cube.vertex_buffer_view);
			command_list->IASetIndexBuffer(&cube.index_buffer_view);
			command_list->DrawIndexedInstanced(cube.index_count(), 1, 0, 0, 0);
		}

		auto specular_pixel_shader_barrier = CD3DX12_RESOURCE_BARRIER::Transition(specular_cubemap_texture.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		command_list->ResourceBarrier(1, &specular_pixel_shader_barrier);
	}

	//Specular LUT
	{
		auto rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(specular_lut_texture.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
		command_list->ResourceBarrier(1, &rt_barrier);
	
		command_list->SetPipelineState(specular_lut_pipeline_state.Get());

		command_list->OMSetRenderTargets(1, specular_lut_texture.rtv_handles.data(), FALSE, nullptr);

		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(specular_lut_size);
		viewport.Height = static_cast<float>(specular_lut_size);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		command_list->RSSetViewports(1, &viewport);

		const D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(specular_lut_size), static_cast<LONG>(specular_lut_size) };
		command_list->RSSetScissorRects(1, &scissor_rect);

		command_list->IASetVertexBuffers(0, 1, &quad.vertex_buffer_view);
		command_list->IASetIndexBuffer(&quad.index_buffer_view);
		command_list->DrawIndexedInstanced(quad.index_count(), 1, 0, 0, 0);

		auto pixel_shader_barrier = CD3DX12_RESOURCE_BARRIER::Transition(specular_lut_texture.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		command_list->ResourceBarrier(1, &pixel_shader_barrier);
	}

	HR_CHECK(command_list->Close());

	ID3D12CommandList* p_cmd_list = command_list.Get();
	command_queue->ExecuteCommandLists(1, &p_cmd_list);
	
	wait_gpu_idle(device, command_queue);

	spherical_to_cube_scene.release();
	spherical_to_cube_instance.release();
	diffuse_convolution_instance.release();
	specular_prefilter_instance.release();

	uint32_t model_to_render = 0;
	const char* model_paths[] = {"data/meshes/sphere.glb", "data/meshes/Monkey.glb", "data/meshes/LunaMoth.glb"};

	std::vector<std::vector<Mesh>> models;

	for (uint32_t i = 0; i < _countof(model_paths); ++i)
	{
		GltfAsset gltf_asset;
		if (!gltf_load_asset(model_paths[i], &gltf_asset))
		{
			printf("FAILED TO LOAD GLTF ASSET\n");
			exit(1);
		}
		assert(gltf_asset.num_meshes > 0);
		
		std::vector<Mesh> meshes;
		GltfMesh* gltf_mesh = &gltf_asset.meshes[0];
		for (uint32_t prim_idx = 0; prim_idx < gltf_mesh->num_primitives; ++prim_idx)
		{
			std::vector<GpuVertex> vertices;
			std::vector<UINT32> indices;
		
			GltfPrimitive* primitive = &gltf_mesh->primitives[prim_idx];
	
			//Vertices
			uint8_t* positions_buffer = primitive->positions->buffer_view->buffer->data;
			positions_buffer += gltf_accessor_get_initial_offset(primitive->positions);
			uint32_t positions_byte_stride = gltf_accessor_get_stride(primitive->positions);
	
			uint8_t* normals_buffer = primitive->normals->buffer_view->buffer->data;
			normals_buffer += gltf_accessor_get_initial_offset(primitive->normals);
			uint32_t normals_byte_stride = gltf_accessor_get_stride(primitive->normals);
	
			uint8_t* uvs_buffer = primitive->texcoord0->buffer_view->buffer->data;
			uvs_buffer += gltf_accessor_get_initial_offset(primitive->texcoord0);
			uint32_t uvs_byte_stride = gltf_accessor_get_stride(primitive->texcoord0);
	
			uint32_t vertices_count = primitive->positions->count;
			vertices.reserve(vertices_count);
	
			for (uint32_t vert_idx = 0; vert_idx < vertices_count; ++vert_idx) 
			{
				GpuVertex vertex;
				memcpy(&vertex.position, positions_buffer, positions_byte_stride);
				memcpy(&vertex.normal, normals_buffer, normals_byte_stride);
				vertex.color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
				memcpy(&vertex.uv, uvs_buffer, uvs_byte_stride);
		
				positions_buffer += positions_byte_stride;
				normals_buffer += normals_byte_stride;
				uvs_buffer += uvs_byte_stride;
	
				vertices.push_back(vertex);
			}
	
			//Indices
			uint8_t* indices_buffer = primitive->indices->buffer_view->buffer->data;
			indices_buffer += gltf_accessor_get_initial_offset(primitive->indices);
			uint32_t indices_byte_stride = gltf_accessor_get_stride(primitive->indices);
	
			uint32_t indices_count = primitive->indices->count;
			indices.reserve(indices_count);
	
			for (uint32_t indices_idx = 0; indices_idx < indices_count; ++indices_idx) 
			{
				UINT32 index = 0; //Need to init for memcpy
				memcpy(&index, indices_buffer, indices_byte_stride);
				indices_buffer += indices_byte_stride;
				indices.push_back(index);
			}

			meshes.emplace_back(Mesh(gpu_memory_allocator, vertices, indices));
		}

		models.push_back(meshes);
	}

	TConstantBufferArray<SceneConstantBuffer, backbuffer_count> scene_constant_buffers(gpu_memory_allocator);

	//Eventually these will hold per-frame data (transform, etc.)
	TConstantBufferArray<MeshRenderConstantBuffer, backbuffer_count> mesh_constant_buffers(gpu_memory_allocator);
	TConstantBufferArray<InstanceConstantBuffer, backbuffer_count> skybox_constant_buffers(gpu_memory_allocator);
	TConstantBufferArray<TextureViewerData, backbuffer_count> texture_viewer_constant_buffers(gpu_memory_allocator);

	//fill texture indices on instance data
	for (size_t i = 0; i < backbuffer_count; ++i)
	{
		auto& data = mesh_constant_buffers.data(i);
		data.diffuse_ibl_texture_index = ibl_cubemap_texture.bindless_index;
		data.specular_ibl_texture_index = specular_cubemap_texture.bindless_index;
		data.specular_ibl_mip_count = specular_cubemap_texture.resource->GetDesc().MipLevels;
		data.specular_lut_texture_index = specular_lut_texture.bindless_index;
	}

	XMVECTOR cam_pos	 = XMVectorSet(0.f, -10.f, 30.f, 1.f);
	XMVECTOR cam_forward = XMVectorSet(0.f, 0.f, -1.f, 0.f);
	XMVECTOR cam_up		 = XMVectorSet(0.f, 1.f, 0.f, 0.f);
	XMVECTOR world_up	 = XMVectorSet(0.f, 1.f, 0.f, 0.f);

	clock_t time = clock();
	double accumulated_delta_time = 0.0f;
	size_t frames_rendered = 0;

	POINT last_mouse_pos = {};

	Texture* current_skybox_texture = &hdr_cubemap_texture;
	int skybox_texture_lod = 0;
	bool draw_skybox = true;
	
	int mesh_instance_count = 100;

	bool use_reference_lut = false;

	Texture* debug_view_texture = &specular_lut_texture;
	bool draw_debug_view_texture = false;
	UINT debug_view_texture_size = 500;
	
	bool should_close = false;
	bool vsync_enabled = true;
	
	while (!should_close && IsWindow(window))
	{	
		RECT client_rect;
		if (GetClientRect(window, &client_rect))
		{
			LONG new_width = client_rect.right - client_rect.left;
			LONG new_height = client_rect.bottom - client_rect.top;
			if (new_width != width || new_height != height)
			{
				width = new_width;
				height = new_height;
				printf("Width: %lu Height: %lu\n", new_width, new_height);

				frame_resources.resize(width, height, device, command_queue, gpu_memory_allocator);
			}
		}
		
		clock_t new_time = clock();
		double delta_time = static_cast<double>(new_time - time) / CLOCKS_PER_SEC;
		time = new_time;

		accumulated_delta_time += delta_time;
		frames_rendered++;

		//Mouse Delta
		POINT mouse_pos;
		GetCursorPos(&mouse_pos);
		const float mouse_delta_x = static_cast<float>(mouse_pos.x - last_mouse_pos.x) / width;
		const float mouse_delta_y = static_cast<float>(mouse_pos.y - last_mouse_pos.y) / height;
		last_mouse_pos = mouse_pos;
		
		// Process any messages in the queue.
		MSG msg = {};
		while (PeekMessage(&msg, window, 0, 0, PM_REMOVE))
		{			
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		//Rendering
		if (width > 0 && height > 0)
		{
			//Only wait for the frame resources we need if we know we need to render (aka have a swapchain with valid dimensions)
			frame_resources.wait_for_previous_frame(command_queue);
			
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			//ImGui Code goes here

			ImGui::Text("FPS: %.f", ImGui::GetIO().Framerate);

			ImGui::Checkbox("vsync", &vsync_enabled);

			if (ImGui::CollapsingHeader("Skybox"))
			{
				ImGui::Indent();
				ImGui::Checkbox("Draw Skybox", &draw_skybox);

				const char* current_skybox_texture_name = current_skybox_texture ? current_skybox_texture->get_name() : "None Selected";
				if (ImGui::BeginCombo("Skybox texture", current_skybox_texture_name))
				{
					Texture* cubemap_textures[] = {&hdr_cubemap_texture, &ibl_cubemap_texture, &specular_cubemap_texture};
					for (uint32_t i = 0; i < _countof(cubemap_textures); ++i)
					{
						const int current_index = current_skybox_texture ? current_skybox_texture->bindless_index : INVALID_INDEX;
						const bool is_selected = current_index != INVALID_INDEX && cubemap_textures[i]->bindless_index == current_index;
				
						if (ImGui::Selectable(cubemap_textures[i]->get_name(), is_selected))
						{
							current_skybox_texture = cubemap_textures[i];
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				if (current_skybox_texture != nullptr)
				{
					auto resource_desc = current_skybox_texture->resource->GetDesc();
					if (ImGui::SliderInt("SkyBox LOD", &skybox_texture_lod, 0, resource_desc.MipLevels - 1))
					{
						skybox_texture_lod = max(0, skybox_texture_lod);
					}
				}
				ImGui::Unindent();
			}

			if (ImGui::CollapsingHeader("Model"))
			{
				ImGui::Indent();
				if (ImGui::BeginCombo("Model to Render", model_paths[static_cast<size_t>(model_to_render)]))
				{
					for (uint32_t i = 0; i < models.size(); ++i)
					{
						const bool is_selected = i == model_to_render;
						if (ImGui::Selectable(model_paths[i], is_selected))
						{
							model_to_render = i;
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				ImGui::SliderInt("Instances", &mesh_instance_count, 1, 100);
				ImGui::Unindent();
			}

			if (ImGui::CollapsingHeader("Texture Debug View"))
			{
				ImGui::Indent();
				ImGui::Checkbox("Draw Debug Texture", &draw_debug_view_texture);

				const char* debug_view_texture_name = debug_view_texture ? debug_view_texture->get_name() : "None Selected";
				if (ImGui::BeginCombo("Texture to View", debug_view_texture_name))
				{
					Texture* debug_view_textures[] = {&specular_lut_texture, &reference_lut};
					for (uint32_t i = 0; i < _countof(debug_view_textures); ++i)
					{
						const int current_index = debug_view_texture ? debug_view_texture->bindless_index : INVALID_INDEX;
						const bool is_selected = current_index != INVALID_INDEX && debug_view_textures[i]->bindless_index == current_index;
				
						if (ImGui::Selectable(debug_view_textures[i]->get_name(), is_selected))
						{
							debug_view_texture = debug_view_textures[i];
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}

					ImGui::EndCombo();
				}

				int tmp = static_cast<int>(debug_view_texture_size);
				if (ImGui::SliderInt("Debug Texture Size", &tmp, 0, min(width, height)))
				{
					debug_view_texture_size = static_cast<UINT>(tmp);
				}
				ImGui::Unindent();
			}

			ImGui::Checkbox("Use Reference LUT", &use_reference_lut);

			ImGui::Render();

			if (window == GetFocus())
			{
				//Free-Camera Controls
				const XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_forward));
				
				float translation_speed = 6.0f * static_cast<float>(delta_time);
				if (is_key_down(VK_SHIFT))
				{
					translation_speed *= 5.0f;
				}
				
				if (is_key_down('W')) { cam_pos += cam_forward * translation_speed; }
				if (is_key_down('S')) { cam_pos -= cam_forward * translation_speed; }
				if (is_key_down('D')) { cam_pos += cam_right   * translation_speed; }
				if (is_key_down('A')) { cam_pos -= cam_right   * translation_speed; }
				if (is_key_down('E')) { cam_pos += cam_up	   * translation_speed; }
				if (is_key_down('Q')) { cam_pos -= cam_up	   * translation_speed; }

				//Cam Rotation Control
				if (is_key_down(VK_RBUTTON))
				{
					const float rot_rate = 4.0f;

					auto pitch_rot = XMMatrixRotationAxis(cam_right, rot_rate * mouse_delta_y);
					auto yaw_rot = XMMatrixRotationAxis(world_up, rot_rate * mouse_delta_x);
					auto total_rot = XMMatrixMultiply(pitch_rot, yaw_rot);
					
					cam_forward = XMVector3Transform(cam_forward, total_rot);
					cam_up = XMVector3Transform(cam_up, total_rot);
				}
				
				if (is_key_down(VK_ESCAPE))
				{
					should_close = true;
				}
			}

			SceneConstantBuffer& scene_cbuffer_data = scene_constant_buffers.data(frame_resources.frame_index);

			XMVECTOR target = cam_pos + cam_forward;
			scene_cbuffer_data.view = XMMatrixLookAtLH(cam_pos, target, cam_up);

			float fov_y = 45.0f;
			float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
			scene_cbuffer_data.proj = XMMatrixPerspectiveFovLH(fov_y, aspect_ratio, 0.01f, 100000.0f);
			
			scene_cbuffer_data.cam_pos = cam_pos;
			scene_cbuffer_data.cam_dir = cam_forward;

			skybox_constant_buffers.data(frame_resources.frame_index).texture_index = current_skybox_texture->bindless_index;
			skybox_constant_buffers.data(frame_resources.frame_index).texture_lod = skybox_texture_lod;

			mesh_constant_buffers.data(frame_resources.frame_index).specular_lut_texture_index = use_reference_lut ? reference_lut.bindless_index : specular_lut_texture.bindless_index;

			texture_viewer_constant_buffers.data(frame_resources.frame_index).texture_index = debug_view_texture ? debug_view_texture->bindless_index : INVALID_INDEX;

			HR_CHECK(command_allocators[frame_resources.frame_index]->Reset());

			HR_CHECK(command_list->Reset(command_allocators[frame_resources.frame_index].Get(), pbr_pipeline_state.Get()));

			// Set necessary state.
			command_list->SetGraphicsRootSignature(bindless_root_signature.Get());

			command_list->SetDescriptorHeaps(_countof(bindless_heaps), bindless_heaps);

			//Slot 0: scene cbuffer
			command_list->SetGraphicsRootConstantBufferView(0, scene_constant_buffers.get_gpu_virtual_address(frame_resources.frame_index));
			//Slot 1: instance cbuffer
			command_list->SetGraphicsRootConstantBufferView(1, mesh_constant_buffers.get_gpu_virtual_address(frame_resources.frame_index));
			//Slot 2: bindless texture table
			command_list->SetGraphicsRootDescriptorTable(2, bindless_resource_manager.get_texture_gpu_handle());
			//Slot 3: bindless cubemap table
			command_list->SetGraphicsRootDescriptorTable(3, bindless_resource_manager.get_cubemap_gpu_handle());

			D3D12_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			command_list->RSSetViewports(1, &viewport);

			const D3D12_RECT scissor_rect = { 0, 0, width, height };
			command_list->RSSetScissorRects(1, &scissor_rect);

			// Indicate that the back buffer will be used as a render target.
			auto present_to_rt_barrier = CD3DX12_RESOURCE_BARRIER::Transition(frame_resources.render_targets[frame_resources.frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			command_list->ResourceBarrier(1, &present_to_rt_barrier);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = frame_resources.rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			const UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			rtv_handle.ptr += frame_resources.frame_index * rtv_heap_offset;
			
			D3D12_CPU_DESCRIPTOR_HANDLE depth_handle = frame_resources.depth_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

			// Record commands.
			command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &depth_handle);
			command_list->ClearDepthStencilView(depth_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			
			const float clear_color[] = { 0.1f, 0.1f, 0.1f, 1.0f };
			command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			
			for (Mesh& mesh : models[static_cast<size_t>(model_to_render)])
			{
				command_list->IASetVertexBuffers(0, 1, &mesh.vertex_buffer_view);
				command_list->IASetIndexBuffer(&mesh.index_buffer_view);
				command_list->DrawIndexedInstanced(mesh.index_count(), mesh_instance_count, 0, 0, 0);
			}

			//Render Skybox
			if (draw_skybox)
			{
				//Set Pipeline State
				command_list->SetPipelineState(skybox_pipeline_state.Get());

				// Skybox instance cbuffer (only working currently because our cubemap + env indices are identical
				command_list->SetGraphicsRootConstantBufferView(1, skybox_constant_buffers.get_gpu_virtual_address(frame_resources.frame_index));

				command_list->IASetVertexBuffers(0, 1, &cube.vertex_buffer_view);
				command_list->IASetIndexBuffer(&cube.index_buffer_view);
				command_list->DrawIndexedInstanced(cube.index_count(), 1, 0, 0, 0);
			}

			//Render Debug Texture TODO: Draw in ImGui?
			if (draw_debug_view_texture && debug_view_texture != nullptr)
			{
				UINT min_screen_dimension = min(width,height);
				UINT actual_display_size = min(debug_view_texture_size, min_screen_dimension);
				
				D3D12_VIEWPORT texture_viewer_viewport = {};
				texture_viewer_viewport.TopLeftX = 0.0f;
				texture_viewer_viewport.TopLeftY = 0.0f;
				texture_viewer_viewport.Width = static_cast<float>(actual_display_size);
				texture_viewer_viewport.Height = static_cast<float>(actual_display_size);
				texture_viewer_viewport.MinDepth = 0.0f;
				texture_viewer_viewport.MaxDepth = 1.0f;
				command_list->RSSetViewports(1, &texture_viewer_viewport);

				const D3D12_RECT texture_viewer_scissor_rect = { 0, 0, actual_display_size, actual_display_size };
				command_list->RSSetScissorRects(1, &texture_viewer_scissor_rect);
				
				//Set Pipeline State
				command_list->SetPipelineState(texture_viewer_pipeline_state.Get());

				//Skybox instance cbuffer (only working currently because our cubemap + env indices are identical
				command_list->SetGraphicsRootConstantBufferView(1, texture_viewer_constant_buffers.get_gpu_virtual_address(frame_resources.frame_index));

				command_list->IASetVertexBuffers(0, 1, &quad.vertex_buffer_view);
				command_list->IASetIndexBuffer(&quad.index_buffer_view);
				command_list->DrawIndexedInstanced(quad.index_count(), 1, 0, 0, 0);
			}

			ID3D12DescriptorHeap* imgui_heaps[] = { imgui_descriptor_heap.Get() };
			command_list->SetDescriptorHeaps(_countof(imgui_heaps), imgui_heaps);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());
			
			// Indicate that the back buffer will now be used to present.
			auto rt_to_present_barrier = CD3DX12_RESOURCE_BARRIER::Transition(frame_resources.render_targets[frame_resources.frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			command_list->ResourceBarrier(1, &rt_to_present_barrier);

			HR_CHECK(command_list->Close());

			// Execute the command list.
			ID3D12CommandList* pp_command_lists[] = { command_list.Get() };
			command_queue->ExecuteCommandLists(_countof(pp_command_lists), pp_command_lists);

			// Present the frame.
			const UINT sync_interval = vsync_enabled ? 1 : 0;
			HR_CHECK(frame_resources.swapchain->Present(sync_interval, 0));
		}
	}

	wait_gpu_idle(device, command_queue);

	printf("FPS: %f\n", static_cast<float>(frames_rendered) / accumulated_delta_time);

	{ //Free all memory allocated with D3D12 Memory Allocator
		bindless_resource_manager.release();
		
		hdr_equirectangular_texture.release();
		
		hdr_cubemap_texture.release();
		ibl_cubemap_texture.release();
		specular_cubemap_texture.release();
		specular_lut_texture.release();
		reference_lut.release();

		for (auto& meshes : models)
		{
			for (Mesh& mesh : meshes)
			{
				mesh.release();
			}
		}

		cube.release();
		quad.release();

		scene_constant_buffers.release();
		mesh_constant_buffers.release();
		skybox_constant_buffers.release();
		texture_viewer_constant_buffers.release();

		frame_resources.depth_texture_allocation->Release();
	}

	gpu_memory_allocator->Release();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	
	return 0;
}