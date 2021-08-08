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
#include <iostream>
#include <vector>
#include <array>
using std::array;

//C
#include "time.h"

//GLB Loader
#include "gltf.h"

//STB Image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

//D3D12 Helpers
#include "D3D12MemAlloc/D3D12MemAlloc.h"
#include "d3dx12.h"

#define HR_CHECK(expr)  \
{\
	HRESULT result = (expr);\
	if (FAILED(result))\
	{\
		std::cout << "FAILED HRESULT. line: " << __LINE__ << " code: " << #expr << " error: " << std::hex << result << std::endl;\
		exit(-1);\
	}\
}\

ComPtr<ID3DBlob> compile_shader(const LPCWSTR file_name, const LPCSTR entry_point, const LPCSTR target)
{
	const UINT shader_compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

	ComPtr<ID3DBlob> out_shader;
	ID3DBlob* error_messages;
	const HRESULT hr = D3DCompileFromFile(file_name, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, target, shader_compile_flags, 0, &out_shader, &error_messages);

	if (FAILED(hr) && error_messages)
	{
		const char* error_message = static_cast<const char*>(error_messages->GetBufferPointer());
		std::cout << error_message << std::endl;
	}

	return out_shader;
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

//TODO: struct to pass around common D3D12 objects
// device
// allocator
// command_queue
// per-frame command lists?
// staging command list?

struct SceneConstantBuffer
{
	XMMATRIX view;
	XMMATRIX proj;
	XMVECTOR cam_pos;
};

struct GpuVertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT4 color;
	// XMFLOAT2 uv; //TODO:
};

D3D12_INPUT_ELEMENT_DESC input_element_descs[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
};

struct Mesh
{
	ComPtr<ID3D12Resource> vertex_buffer;
	D3D12MA::Allocation* vertex_buffer_allocation = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	
	ComPtr<ID3D12Resource> index_buffer;
	D3D12MA::Allocation* index_buffer_allocation = nullptr;
	D3D12_INDEX_BUFFER_VIEW index_buffer_view;

	//FCS TODO: Template factory function, to take in different types of vertices
	Mesh(D3D12MA::Allocator* gpu_memory_allocator, std::vector<GpuVertex> vertices, std::vector<UINT32> indices)
	{
		D3D12_RESOURCE_DESC resource_desc = {};
		resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource_desc.Alignment = 0;
		resource_desc.Height = 1;
		resource_desc.DepthOrArraySize = 1;
		resource_desc.MipLevels = 1;
		resource_desc.Format = DXGI_FORMAT_UNKNOWN;
		resource_desc.SampleDesc.Count = 1;
		resource_desc.SampleDesc.Quality = 0;
		resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		
		// FCS TODO: Need to upload with upload heap, then copy over to D3D12_HEAP_TYPE_DEFAULT (Staging Buffer)
		
		D3D12MA::ALLOCATION_DESC alloc_desc = {};
		alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

		const size_t vertices_size = sizeof(GpuVertex) * vertices.size();

		//update resource desc to vertices_size
		resource_desc.Width = vertices_size; 

		//Create our vertex buffer	
		HR_CHECK(gpu_memory_allocator->CreateResource(
			&alloc_desc,
			&resource_desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			&vertex_buffer_allocation,
			IID_PPV_ARGS(&vertex_buffer)
		));

		D3D12_RANGE read_range = { 0, 0 }; // We do not intend to read from these reources on the CPU.

		// Copy the triangle data to the vertex buffer.
		UINT8* vertex_data_begin;
		HR_CHECK(vertex_buffer->Map(0, &read_range, reinterpret_cast<void**>(&vertex_data_begin)));
		memcpy(vertex_data_begin, vertices.data(), vertices_size);
		vertex_buffer->Unmap(0, nullptr);

		// Init the vertex buffer view.
		vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
		vertex_buffer_view.StrideInBytes = sizeof(GpuVertex);
		vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertices_size);

		const size_t indices_size = sizeof(UINT32) * indices.size();

		//update resource desc to indices_size
		resource_desc.Width = indices_size;
		
		// Create our index buffer
		HR_CHECK(gpu_memory_allocator->CreateResource(
	        &alloc_desc,
	        &resource_desc,
	        D3D12_RESOURCE_STATE_GENERIC_READ,
	        nullptr,
	        &index_buffer_allocation,
	        IID_PPV_ARGS(&index_buffer)
	    ));

		//Copy index data
		UINT8* index_data_begin;
		HR_CHECK(index_buffer->Map(0, &read_range, reinterpret_cast<void**>(&index_data_begin)));
		memcpy(index_data_begin, indices.data(), indices_size);
		index_buffer->Unmap(0, nullptr);

		//Init the index buffer view
		index_buffer_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
		index_buffer_view.SizeInBytes = static_cast<UINT>(indices_size);
		index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
	}

	void release()
	{
		if (vertex_buffer_allocation)
		{
			vertex_buffer_allocation->Release();
			vertex_buffer_allocation = nullptr;
		}
		if (index_buffer_allocation)
		{
			index_buffer_allocation->Release();
			index_buffer_allocation = nullptr;
		}
	}
};

struct TextureResource
{
	ComPtr<ID3D12Resource> texture_resource;
	D3D12MA::Allocation* texture_allocation = nullptr;

	//TODO: Won't need this SRV heap when we go bindless
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_srv;

	//TODO: this likely shouldn't be managed by the texture resource
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_rtv;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
	
	TextureResource(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, const UINT image_width, const UINT image_height, const UINT image_count)
	{
		create_texture(device, gpu_memory_allocator, format, flags, image_width, image_height, image_count);
	}

	//Loads a single texture from file
	TextureResource(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, ComPtr<ID3D12CommandQueue> command_queue, const DXGI_FORMAT format, const char* file)
	{
		int image_width, image_height;
		const int desired_channels = 4; //We want an alpha channel

		stbi_set_flip_vertically_on_load(true);
		if (float *image_data = stbi_loadf(file, &image_width, &image_height, nullptr, desired_channels))
		{
			create_texture(device, gpu_memory_allocator, format, D3D12_RESOURCE_FLAG_NONE, image_width, image_height, 1);
			
			const size_t image_pixel_size = desired_channels * sizeof(float);

			ComPtr<ID3D12Resource> staging_buffer;
			D3D12MA::Allocation* staging_buffer_allocation = nullptr;

			//Create staging buffer
			D3D12MA::ALLOCATION_DESC staging_buffer_alloc_desc = {};
			staging_buffer_alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC staging_buffer_desc = {};
			staging_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			staging_buffer_desc.Alignment = 0;
			staging_buffer_desc.Width =  GetRequiredIntermediateSize(texture_resource.Get(),0,1); //Important
			staging_buffer_desc.Height = 1;
			staging_buffer_desc.DepthOrArraySize = 1;
			staging_buffer_desc.MipLevels = 1;
			staging_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
			staging_buffer_desc.SampleDesc.Count = 1;
			staging_buffer_desc.SampleDesc.Quality = 0;
			staging_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			staging_buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

			HR_CHECK(gpu_memory_allocator->CreateResource(
                &staging_buffer_alloc_desc,
                &staging_buffer_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                &staging_buffer_allocation,
                IID_PPV_ARGS(&staging_buffer)
            ));

			//TODO: Store our "Transfer" command list and reuse
			ComPtr<ID3D12CommandAllocator> command_allocator;
			HR_CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));

			ComPtr<ID3D12GraphicsCommandList> command_list;
			HR_CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list)));
			command_list->Close();
			command_list->Reset(command_allocator.Get(), nullptr);

			D3D12_SUBRESOURCE_DATA subresource_data = {};
			subresource_data.pData = image_data;
			subresource_data.RowPitch = image_width * image_pixel_size;
			subresource_data.SlicePitch = subresource_data.RowPitch * image_height;
			
			UpdateSubresources(command_list.Get(), texture_resource.Get(), staging_buffer.Get(), 0, 0, 1, &subresource_data);
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            command_list->ResourceBarrier(1, &barrier);

			command_list->Close();

			ID3D12CommandList* p_cmd_list = command_list.Get();
			command_queue->ExecuteCommandLists(1, &p_cmd_list);
			
			wait_gpu_idle(device, command_queue);
			
			staging_buffer_allocation->Release();
			
			stbi_image_free(image_data);
		}
		else
		{
			std::cout << "Failed to load texture: " << file << std::endl;
		}
	}

	void create_texture(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, const UINT image_width, const UINT image_height, const UINT image_count)
	{
		assert(gpu_memory_allocator);
		assert(image_count > 0);
		
		D3D12MA::ALLOCATION_DESC texture_alloc_desc = {};
		texture_alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC texture_desc = {};
		texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texture_desc.Alignment = 0;
		texture_desc.Width = image_width;
		texture_desc.Height = image_height;
		texture_desc.DepthOrArraySize = image_count;
		texture_desc.MipLevels = 1;
		texture_desc.Format = format;
		texture_desc.SampleDesc.Count = 1;
		texture_desc.SampleDesc.Quality = 0;
		texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texture_desc.Flags = flags;

		D3D12_CLEAR_VALUE clear_value = {};
		clear_value.Format = format;

		const bool can_have_clear_value = (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
									   || (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		HR_CHECK(gpu_memory_allocator->CreateResource(
            &texture_alloc_desc,
            &texture_desc,
            D3D12_RESOURCE_STATE_COMMON,
            can_have_clear_value ? &clear_value : nullptr,
            &texture_allocation,
            IID_PPV_ARGS(&texture_resource)
        ));

		create_srv_descriptor_heap(device);

		//Create rtv if we can be a render target
		if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
		{
			create_rtv_descriptor_heap(device);
		}
	}

	//TODO: Should likely be separated from TextureResource struct
	//Creates a basic SRV for this texture resource
	//TODO: RTV
	void create_srv_descriptor_heap(const ComPtr<ID3D12Device> device)
	{
		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = 1;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&texture_descriptor_heap_srv)));

		const UINT16 texture_array_size = texture_resource->GetDesc().DepthOrArraySize;
		const bool is_texture_array = texture_array_size > 1;

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};

		//Shared State
		srv_desc.Format = texture_resource->GetDesc().Format;
		srv_desc.ViewDimension = is_texture_array ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		
		if (is_texture_array)
		{	
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.MipLevels = 1;
			srv_desc.Texture2DArray.FirstArraySlice = 0;
			srv_desc.Texture2DArray.ArraySize = texture_array_size;
			srv_desc.Texture2DArray.PlaneSlice = 0;
			srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
		}
		else
		{
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = 1;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		device->CreateShaderResourceView(texture_resource.Get(), &srv_desc, texture_descriptor_heap_srv->GetCPUDescriptorHandleForHeapStart());
	}

	void create_rtv_descriptor_heap(const ComPtr<ID3D12Device> device)
	{
		D3D12_RESOURCE_DESC resource_desc = texture_resource->GetDesc();
		assert(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET == D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		
		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = resource_desc.DepthOrArraySize;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&texture_descriptor_heap_rtv)));
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle = texture_descriptor_heap_rtv->GetCPUDescriptorHandleForHeapStart();
		UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		const UINT16 texture_array_size = resource_desc.DepthOrArraySize;
		const bool is_texture_array = texture_array_size > 1;

		D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};

		//Shared State
		rtv_desc.Format = resource_desc.Format;
		rtv_desc.ViewDimension = is_texture_array ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
		
		for (UINT i = 0; i < resource_desc.DepthOrArraySize; ++i)
		{
			if (is_texture_array)
			{
				//Treating texture array RTVs as individual 'views' into each slice
				rtv_desc.Texture2DArray.MipSlice = 0;
				rtv_desc.Texture2DArray.FirstArraySlice = i;
				rtv_desc.Texture2DArray.ArraySize = 1;
				rtv_desc.Texture2DArray.PlaneSlice = 0;
			}
			else
			{
				rtv_desc.Texture2D.MipSlice = 0;
				rtv_desc.Texture2D.PlaneSlice = 0;
			}
			device->CreateRenderTargetView(texture_resource.Get(), &rtv_desc, rtv_descriptor_handle);
			rtv_handles.push_back(rtv_descriptor_handle);
			rtv_descriptor_handle.ptr += rtv_heap_offset;
		}
	}

	void set_name(const LPCWSTR in_name) const
	{
		assert(texture_resource);
		texture_resource->SetName(in_name);
	}
	
	void release()
	{
		if (texture_allocation)
		{
			texture_allocation->Release();
			texture_allocation = nullptr;
		}
	}
};

//TODO: TextureBuilder:
/* texture_builder
	.width(4)
	.height(4)
	.format(...)
	.array_count(6)
	.allow_render_target(true)
	.build();
*/

static const UINT backbuffer_count = 3;

/*  Almost identical to CD3DX12_RESOURCE_BARRIER::Transition in d3dx12.h
	(helper file provided by microsoft that we've opted to not use) */
static D3D12_RESOURCE_BARRIER transition_resource(
	ID3D12Resource* in_resource,
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
		L"D3D12 Testbed",
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
	ComPtr<ID3D12Debug1> debug_controller_1;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
	{
		debug_controller->EnableDebugLayer();
		
		if (SUCCEEDED(debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller_1))))
		{
			debug_controller_1->SetEnableGPUBasedValidation(true);
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
	
	HANDLE fence_event;
	ComPtr<ID3D12Fence> fence;

	UINT64 fence_values[backbuffer_count];
	memset(fence_values, 0, sizeof(UINT64) * backbuffer_count);

	// 3. Create a command queue
	ComPtr<ID3D12CommandQueue> command_queue;
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HR_CHECK(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));

	// 4. Create the swapchain and associate it with our window
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.BufferCount = backbuffer_count;
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

	// 5. Create descriptor heaps (1 per frame for CBV desc heaps)
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
	rtv_heap_desc.NumDescriptors = backbuffer_count;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap;
	HR_CHECK(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_descriptor_heap)));

	UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	
	// array<ComPtr<ID3D12DescriptorHeap>, backbuffer_count> cbv_descriptor_heaps;
	// for (uint32_t i = 0; i < backbuffer_count; ++i)
	// {
	// 	D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = {};
	// 	desc_heap_desc.NumDescriptors = 1;
	// 	desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	// 	desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	// 	HR_CHECK(device->CreateDescriptorHeap(&desc_heap_desc, IID_PPV_ARGS(&cbv_descriptor_heaps[i])));
	// }

	// Create Depth Buffer
	D3D12_DESCRIPTOR_HEAP_DESC depth_heap_desc = {};
	depth_heap_desc.NumDescriptors = 1;
	depth_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	depth_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ComPtr<ID3D12DescriptorHeap> depth_descriptor_heap;
	HR_CHECK(device->CreateDescriptorHeap(&depth_heap_desc, IID_PPV_ARGS(&depth_descriptor_heap)));

	D3D12MA::ALLOCATION_DESC depth_alloc_desc = {};
	depth_alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC depth_resource_desc = {};
	depth_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depth_resource_desc.Alignment = 0;
	depth_resource_desc.Width = width;
	depth_resource_desc.Height = height;
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

	ComPtr<ID3D12Resource> depth_texture;
	D3D12MA::Allocation* depth_texture_allocation = nullptr;
	gpu_memory_allocator->CreateResource(
		&depth_alloc_desc,
		&depth_resource_desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depth_clear_value,
		&depth_texture_allocation,
		IID_PPV_ARGS(&depth_texture)
	);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
	dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
	dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv_desc.Flags = D3D12_DSV_FLAG_NONE;

	device->CreateDepthStencilView(depth_texture.Get(), &dsv_desc, depth_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

	array<ComPtr<ID3D12CommandAllocator>, backbuffer_count> command_allocators;
	array<ComPtr<ID3D12Resource>, backbuffer_count> render_targets;
	// 6. Create render target views (1 per frame), associating them with corresponding rtv descriptor heaps
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle(rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());

		// Create a render target view for each frame.
		for (UINT current_frame_index = 0; current_frame_index < backbuffer_count; current_frame_index++)
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
		std::array<CD3DX12_ROOT_PARAMETER1,2> root_parameters;

		//TODO: Could store multiple in one table (via CD3DX12_ROOT_PARAMETER1::InitAsDescriptorTable)

		// Constant Buffer View
		root_parameters[0].InitAsConstantBufferView(0,0,D3D12_ROOT_DESCRIPTOR_FLAG_NONE,D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_DESCRIPTOR_RANGE1 range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
		root_parameters[1].InitAsDescriptorTable (1, &range);
		
		std::array<CD3DX12_STATIC_SAMPLER_DESC,1> samplers;
		samplers[0].Init(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);
		
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(root_parameters.size(), root_parameters.data(), samplers.size(), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		
		ComPtr<ID3DBlob> signature_blob, error_blob;
		HR_CHECK(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature_blob, &error_blob));
		HR_CHECK(device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature)));
	}

	// 9. Compile our vertex and pixel shaders
	ComPtr<ID3DBlob> vertex_shader = compile_shader(L"data/shaders/shaders.hlsl", "vs_main", "vs_5_0");
	ComPtr<ID3DBlob> pixel_shader  = compile_shader(L"data/shaders/shaders.hlsl", "ps_main", "ps_5_0");

	assert(vertex_shader && pixel_shader);

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
	pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso_desc.DepthStencilState.DepthEnable = TRUE;
	pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	pso_desc.DepthStencilState.StencilEnable = FALSE;
	
	pso_desc.SampleMask = UINT_MAX;
	pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso_desc.NumRenderTargets = 1;
	pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pso_desc.SampleDesc.Count = 1;

	ComPtr<ID3D12PipelineState> pipeline_state;
	HR_CHECK(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));

	// 12. Create Command list using command allocator and pipeline state, and close it (we'll record it later)
	//FCS TODO: one command_list per frame_index, build command_list per-frame (may not need this, one command allocator per frame should be sufficient, as you can reset command lists after they've been submitted)
	ComPtr<ID3D12GraphicsCommandList> command_list;
	HR_CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[frame_index].Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list)));
	HR_CHECK(command_list->Close());

	//Load Environment Map
	//TODO: remove this once we get our cubemap convolved
	TextureResource ibl_diffuse_texture(device, gpu_memory_allocator, command_queue, DXGI_FORMAT_R32G32B32A32_FLOAT, "data/hdr/Frozen_Waterfall_Env.hdr");
	ibl_diffuse_texture.set_name(TEXT("Filtered Env Map (equirectangular)"));

	TextureResource hdr_texture(device, gpu_memory_allocator, command_queue, DXGI_FORMAT_R32G32B32A32_FLOAT, "data/hdr/Frozen_Waterfall_Ref.hdr");
	hdr_texture.set_name(TEXT("Env Map (equirectangular)"));

	const UINT cube_size = 512;
	DXGI_FORMAT cubemap_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	TextureResource cubemap_texture(device, gpu_memory_allocator, cubemap_format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, cube_size, cube_size, 6);
	cubemap_texture.set_name(TEXT("Cubemap Texture"));

	//TODO: Render Cube faces using equirectangular map

	//Setup cube
	//TODO: We render all faces at once, so we really only need one "Face" to rasterize (4 verts, 6 indices)
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

	//TODO: look into pipeline state streams?
	struct SimplePipelineStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE p_root_signature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT input_layout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitive_topology;
		CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
		CD3DX12_PIPELINE_STATE_STREAM_VS vs;
		CD3DX12_PIPELINE_STATE_STREAM_PS ps;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtv_formats;
	};

	ComPtr<ID3D12RootSignature> cubemap_root_signature;
	{
		std::array<CD3DX12_ROOT_PARAMETER1,2> root_parameters;

		// Constant Buffer View
		root_parameters[0].InitAsConstantBufferView(0,0,D3D12_ROOT_DESCRIPTOR_FLAG_NONE,D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_DESCRIPTOR_RANGE1 range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };
		root_parameters[1].InitAsDescriptorTable(1, &range);
		
		std::array<CD3DX12_STATIC_SAMPLER_DESC,1> samplers;
		samplers[0].Init(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT);
		
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(root_parameters.size(), root_parameters.data(), samplers.size(), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		
		ComPtr<ID3DBlob> signature_blob, error_blob;
		HR_CHECK(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature_blob, &error_blob));
		HR_CHECK(device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&cubemap_root_signature)));
	}

	ComPtr<ID3DBlob> render_cubemap_vs = compile_shader(L"data/shaders/render_to_cubemap.hlsl", "vs_main", "vs_5_0");
	ComPtr<ID3DBlob> render_cubemap_ps = compile_shader(L"data/shaders/render_to_cubemap.hlsl", "ps_main", "ps_5_0");

	D3D12_RT_FORMAT_ARRAY rtv_formats = {};
	rtv_formats.NumRenderTargets = 6;
	for (UINT i = 0; i < rtv_formats.NumRenderTargets; ++i)
	{
		rtv_formats.RTFormats[i] = cubemap_format;
	}

	CD3DX12_RASTERIZER_DESC cube_rasterizer = {};
	cube_rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	cube_rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	cube_rasterizer.FrontCounterClockwise = FALSE;
	cube_rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	cube_rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	cube_rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	cube_rasterizer.DepthClipEnable = FALSE;
	cube_rasterizer.MultisampleEnable = FALSE;
	cube_rasterizer.AntialiasedLineEnable = FALSE;
	cube_rasterizer.ForcedSampleCount = 0;
	cube_rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	SimplePipelineStream render_cube_pipe_stream;
	render_cube_pipe_stream.p_root_signature = cubemap_root_signature.Get();
	render_cube_pipe_stream.input_layout = { input_element_descs, _countof(input_element_descs) };
	render_cube_pipe_stream.primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	render_cube_pipe_stream.rasterizer = cube_rasterizer;
	render_cube_pipe_stream.vs = CD3DX12_SHADER_BYTECODE(render_cubemap_vs.Get());
	render_cube_pipe_stream.ps = CD3DX12_SHADER_BYTECODE(render_cubemap_ps.Get());
	render_cube_pipe_stream.rtv_formats = rtv_formats;

	D3D12_PIPELINE_STATE_STREAM_DESC render_cube_stream_desc = {
		sizeof(render_cube_pipe_stream), &render_cube_pipe_stream
    };

	ComPtr<ID3D12PipelineState> render_cubemap_pipeline_state;
	ComPtr<ID3D12Device2> device2;
	HR_CHECK(device->QueryInterface(IID_PPV_ARGS(&device2)));
	HR_CHECK(device2->CreatePipelineState(&render_cube_stream_desc, IID_PPV_ARGS(&render_cubemap_pipeline_state)));

	D3D12MA::ALLOCATION_DESC cube_cbuffer_alloc_desc = {};
	cube_cbuffer_alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
 
	D3D12_RESOURCE_DESC cube_cbuffer_resource_desc = {};
	cube_cbuffer_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cube_cbuffer_resource_desc.Alignment = 0;
	cube_cbuffer_resource_desc.Width = (sizeof(SceneConstantBuffer)+ 255) & ~255;
	cube_cbuffer_resource_desc.Height = 1;
	cube_cbuffer_resource_desc.DepthOrArraySize = 1;
	cube_cbuffer_resource_desc.MipLevels = 1;
	cube_cbuffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	cube_cbuffer_resource_desc.SampleDesc.Count = 1;
	cube_cbuffer_resource_desc.SampleDesc.Quality = 0;
	cube_cbuffer_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cube_cbuffer_resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	
	ComPtr<ID3D12Resource> cube_cbuffer;
	D3D12MA::Allocation* cube_cbuffer_allocation;
	UINT8* cube_cbuffer_address = nullptr;
 
	HR_CHECK(gpu_memory_allocator->CreateResource(
        &cube_cbuffer_alloc_desc,
        &cube_cbuffer_resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        &cube_cbuffer_allocation,
        IID_PPV_ARGS(&cube_cbuffer)
    ));
 
	D3D12_RANGE read_range = { 0, 0 }; // We do not intend to read from these reources on the CPU.
	HR_CHECK(cube_cbuffer->Map(0, &read_range, reinterpret_cast<void**>(&cube_cbuffer_address)));

	XMVECTOR cube_cam_pos	  = XMVectorSet(0.f, 0.f, 0.f, 1.f);
	XMVECTOR cube_cam_forward = XMVectorSet(0.f, 0.f, -1.f, 0.f);
	XMVECTOR cube_cam_up	  = XMVectorSet(0.f, 1.f, 0.f, 0.f);

	SceneConstantBuffer cube_cbuffer_data;
	cube_cbuffer_data.view = XMMatrixLookAtLH(cube_cam_pos, cube_cam_forward, cube_cam_up);
	cube_cbuffer_data.proj = XMMatrixIdentity();
	memcpy(cube_cbuffer_address, &cube_cbuffer_data, sizeof(cube_cbuffer_data));

	HR_CHECK(command_list->Reset(command_allocators[frame_index].Get(), render_cubemap_pipeline_state.Get()));

	D3D12_RESOURCE_BARRIER cubemap_rt_barrier = transition_resource(cubemap_texture.texture_resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
	command_list->ResourceBarrier(1, &cubemap_rt_barrier);
	command_list->OMSetRenderTargets(cubemap_texture.rtv_handles.size(), cubemap_texture.rtv_handles.data(), FALSE, nullptr);
			
	// const float clear_color[] = { 0.0f, 0.1f, 0.2f, 1.0f };
	// command_list->ClearRenderTargetView(cubemap_rtv_handle, clear_color, 0, nullptr);
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	command_list->SetGraphicsRootSignature(cubemap_root_signature.Get());

	ID3D12DescriptorHeap* cubemap_descriptor_heaps[] = { hdr_texture.texture_descriptor_heap_srv.Get() };
	command_list->SetDescriptorHeaps(_countof(cubemap_descriptor_heaps), cubemap_descriptor_heaps);

	//Slot 0: constant buffer view
	command_list->SetGraphicsRootConstantBufferView(0, cube_cbuffer->GetGPUVirtualAddress());
	//Slot 1: hdr texture
	command_list->SetGraphicsRootDescriptorTable(1, hdr_texture.texture_descriptor_heap_srv->GetGPUDescriptorHandleForHeapStart());

	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(cube_size);
	viewport.Height = static_cast<float>(cube_size);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	command_list->RSSetViewports(1, &viewport);

	D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(cube_size), static_cast<LONG>(cube_size) };
	command_list->RSSetScissorRects(1, &scissor_rect);

	command_list->IASetVertexBuffers(0, 1, &cube.vertex_buffer_view);
	command_list->IASetIndexBuffer(&cube.index_buffer_view);
	const UINT num_cube_indices = cube.index_buffer_view.SizeInBytes / sizeof(UINT32);
	command_list->DrawIndexedInstanced(num_cube_indices, 1, 0, 0, 0);

	HR_CHECK(command_list->Close());

	ID3D12CommandList* p_cmd_list = command_list.Get();
	command_queue->ExecuteCommandLists(1, &p_cmd_list);
	wait_gpu_idle(device, command_queue);

	cube_cbuffer_allocation->Release();

	GltfAsset gltf_asset;
	if (!gltf_load_asset("data/meshes/sphere.glb", &gltf_asset))
	{
		std::cout << "FAILED TO LOAD GLTF ASSET" << std::endl;
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
			vertex.color = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
			// memcpy(&vertex.uv, uvs_buffer, uvs_byte_stride); //TODO:
		
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

	// Note: using upload heaps to transfer static data like vert buffers is not 
	// recommended. Every time the GPU needs it, the upload heap will be marshalled 
	// over. Please read up on Default Heap usage. An upload heap is used here for 
	// code simplicity and because there are very few verts to actually transfer.
	
	D3D12MA::ALLOCATION_DESC alloc_desc = {};
	alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

	// 14. Create Constant Buffer
	D3D12_RESOURCE_DESC resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Alignment = 0;
	resource_desc.Width = (sizeof(SceneConstantBuffer)+ 255) & ~255;
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.SampleDesc.Quality = 0;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	array<ComPtr<ID3D12Resource>,backbuffer_count> constant_buffers;
	D3D12MA::Allocation* constant_buffer_allocations[backbuffer_count];
	UINT8* cbv_gpu_addresses[backbuffer_count];

	for (uint32_t i = 0; i < backbuffer_count; ++i)
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
		// D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		// cbv_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
		// cbv_desc.SizeInBytes = static_cast<UINT>(resource_desc.Width);
		// device->CreateConstantBufferView(&cbv_desc, cbv_descriptor_heaps[i]->GetCPUDescriptorHandleForHeapStart());

		// Map and initialize the constant buffer. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		D3D12_RANGE read_range = { 0, 0 }; // We do not intend to read from these reources on the CPU.
		HR_CHECK(constant_buffer->Map(0, &read_range, reinterpret_cast<void**>(&cbv_gpu_addresses[i])));
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		for (uint32_t i = 0; i < backbuffer_count; ++i)
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

	XMVECTOR cam_pos	 = XMVectorSet(0.f, -10.f, 30.f, 1.f);
	XMVECTOR cam_forward = XMVectorSet(0.f, 0.f, -1.f, 0.f);
	XMVECTOR cam_up		 = XMVectorSet(0.f, 1.f, 0.f, 0.f);

	SceneConstantBuffer scene_constant_buffer_data = {};

	clock_t time = clock();
	double accumulated_delta_time = 0.0f;
	size_t frames_rendered = 0;
	
	while (!should_close)
	{
		//TODO: RenderTarget Resizing
		
		clock_t new_time = clock();
		double delta_time = static_cast<double>(new_time - time) / CLOCKS_PER_SEC;
		time = new_time;

		accumulated_delta_time += delta_time;
		frames_rendered++;
		
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

			{	//Camera Control
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

				//FCS TODO: Cam Rotation Control
			}

			//FCS TODO: FIXME: Bad matrix multiplies messing with depth test?

			XMVECTOR target = cam_pos + cam_forward;
			scene_constant_buffer_data.view = XMMatrixLookAtLH(cam_pos, target, cam_up);

			float fov_y = 45.0f;// * XM_PI / 180.0f;
			float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
			scene_constant_buffer_data.proj = XMMatrixPerspectiveFovLH(fov_y, aspect_ratio, 0.01f, 1000.0f);
			
			scene_constant_buffer_data.cam_pos = cam_pos;
			memcpy(cbv_gpu_addresses[frame_index], &scene_constant_buffer_data, sizeof(scene_constant_buffer_data));

			HR_CHECK(command_allocators[frame_index]->Reset());

			HR_CHECK(command_list->Reset(command_allocators[frame_index].Get(), pipeline_state.Get()));

			// Set necessary state.
			command_list->SetGraphicsRootSignature(root_signature.Get());

			//FCS NOTE: Only one heap of a given type can be set at a time, so we'll eventually want one big heap
			ID3D12DescriptorHeap* descriptor_heaps[] = { ibl_diffuse_texture.texture_descriptor_heap_srv.Get() };
			command_list->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

			//Slot 0: constant buffer view
			command_list->SetGraphicsRootConstantBufferView(0, constant_buffers[frame_index]->GetGPUVirtualAddress());
			//Slot 1: hdr texture
			command_list->SetGraphicsRootDescriptorTable(1, ibl_diffuse_texture.texture_descriptor_heap_srv->GetGPUDescriptorHandleForHeapStart());

			D3D12_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			command_list->RSSetViewports(1, &viewport);

			D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			command_list->RSSetScissorRects(1, &scissor_rect);

			// Indicate that the back buffer will be used as a render target.
			D3D12_RESOURCE_BARRIER present_to_rt_barrier = transition_resource(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			command_list->ResourceBarrier(1, &present_to_rt_barrier);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			rtv_handle.ptr += frame_index * rtv_heap_offset;
			
			D3D12_CPU_DESCRIPTOR_HANDLE depth_handle = depth_descriptor_heap->GetCPUDescriptorHandleForHeapStart();

			// Record commands.
			command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &depth_handle);
			command_list->ClearDepthStencilView(depth_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			
			const float clear_color[] = { 0.0f, 0.1f, 0.2f, 1.0f };
			command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
			command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			for (Mesh& mesh : meshes)
			{
				command_list->IASetVertexBuffers(0, 1, &mesh.vertex_buffer_view);
				command_list->IASetIndexBuffer(&mesh.index_buffer_view);
				UINT num_indices = mesh.index_buffer_view.SizeInBytes / sizeof(UINT32);
				command_list->DrawIndexedInstanced(num_indices, 100, 0, 0, 0);
			}
			
			// Indicate that the back buffer will now be used to present.
			D3D12_RESOURCE_BARRIER rt_to_present_barrier = transition_resource(render_targets[frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			command_list->ResourceBarrier(1, &rt_to_present_barrier);

			HR_CHECK(command_list->Close());

			// Execute the command list.
			ID3D12CommandList* pp_command_lists[] = { command_list.Get() };
			command_queue->ExecuteCommandLists(_countof(pp_command_lists), pp_command_lists);

			// Present the frame.
			const bool vsync_enabled = true;
			const UINT sync_interval = vsync_enabled ? 1 : 0;
			HR_CHECK(swapchain->Present(sync_interval, 0));

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
		ibl_diffuse_texture.release();
		hdr_texture.release();
		cubemap_texture.release();
		
		for (Mesh& mesh : meshes)
		{
			mesh.release();
		}

		cube.release();

		for (UINT i = 0; i < backbuffer_count; ++i)
		{
			constant_buffer_allocations[i]->Release();
		}

		depth_texture_allocation->Release();
	}

	gpu_memory_allocator->Release();
	
	return 0;
}