#pragma once

#include <wrl.h>
using Microsoft::WRL::ComPtr;

#include <d3d12.h>
#include <D3Dcompiler.h>
#include "D3D12MemAlloc/D3D12MemAlloc.h"
#include "d3dx12.h"

#include <cstdio>
#include <cassert>

//STB Image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define HR_CHECK(expr)  \
{\
HRESULT result = (expr);\
if (FAILED(result))\
{\
printf("FAILED HRESULT: Line: %i Code: %s Error: %x\n", __LINE__, #expr, result); \
exit(-1);\
}\
}\

// We do not intend to read from these resources on the CPU.
static const D3D12_RANGE no_read_range = { 0, 0 };

inline ComPtr<ID3DBlob> compile_shader(const LPCWSTR file_name, const LPCSTR entry_point, const LPCSTR target)
{
	const UINT shader_compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

	ComPtr<ID3DBlob> out_shader;
	ID3DBlob* error_messages;
	const HRESULT hr = D3DCompileFromFile(file_name, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, target, shader_compile_flags, 0, &out_shader, &error_messages);

	if (FAILED(hr) && error_messages)
	{
		const char* error_message = static_cast<const char*>(error_messages->GetBufferPointer());
		printf("CompileShader Error: %s\n", error_message);
	}

	return out_shader;
}

inline void wait_gpu_idle(ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandQueue> command_queue)
{
	ComPtr<ID3D12Fence> fence;
	HR_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

	const HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fence_event);
	
	HR_CHECK(command_queue->Signal(fence.Get(),1));
	HR_CHECK(fence->SetEventOnCompletion(1, fence_event));
	WaitForSingleObject(fence_event, INFINITE);
}

struct Mesh
{
	ComPtr<ID3D12Resource> vertex_buffer;
	D3D12MA::Allocation* vertex_buffer_allocation = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	
	ComPtr<ID3D12Resource> index_buffer;
	D3D12MA::Allocation* index_buffer_allocation = nullptr;
	D3D12_INDEX_BUFFER_VIEW index_buffer_view;

	template <typename T>
	Mesh(D3D12MA::Allocator* gpu_memory_allocator, std::vector<T> vertices, std::vector<UINT32> indices)
	{
		static_assert(!std::is_pointer<T>(), "vertices must be an array to some non-pointer type");
		
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

		const size_t vertices_size = sizeof(T) * vertices.size();

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
		vertex_buffer->SetName(TEXT("mesh vertex buffer"));
		vertex_buffer_allocation->SetName(TEXT("mesh vertex buffer memory"));

		// Copy the triangle data to the vertex buffer.
		UINT8* vertex_data_begin;
		HR_CHECK(vertex_buffer->Map(0, &no_read_range, reinterpret_cast<void**>(&vertex_data_begin)));
		memcpy(vertex_data_begin, vertices.data(), vertices_size);
		vertex_buffer->Unmap(0, nullptr);

		// Init the vertex buffer view.
		vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
		vertex_buffer_view.StrideInBytes = sizeof(T);
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
		index_buffer->SetName(TEXT("mesh index buffer"));
		index_buffer->SetName(TEXT("mesh index buffer memory"));

		//Copy index data
		UINT8* index_data_begin;
		HR_CHECK(index_buffer->Map(0, &no_read_range, reinterpret_cast<void**>(&index_data_begin)));
		memcpy(index_data_begin, indices.data(), indices_size);
		index_buffer->Unmap(0, nullptr);

		//Init the index buffer view
		index_buffer_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
		index_buffer_view.SizeInBytes = static_cast<UINT>(indices_size);
		index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
	}

	UINT index_count() const
	{
		return index_buffer_view.SizeInBytes / sizeof(UINT32);
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

//TODO: TextureBuilder:
/* texture_builder
.width(4)
.height(4)
.format(...)
.array_count(6)
.allow_render_target(true)
.build();
*/

struct TextureResource
{
	ComPtr<ID3D12Resource> texture_resource;
	D3D12MA::Allocation* texture_allocation = nullptr;

	//TODO: Won't need this SRV heap when we go bindless
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_srv;
	ComPtr<ID3D12DescriptorHeap> cubemap_descriptor_heap_srv;

	//TODO: this likely shouldn't be managed by the texture resource
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_rtv;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
	
	TextureResource(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, const UINT image_width, const UINT image_height, const UINT image_count)
	{
		create_texture(device, gpu_memory_allocator, format, flags, image_width, image_height, image_count);
		set_name(TEXT("DefaultTexture"));
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
			set_name(TEXT("FileTexture"));
		}
		else
		{
			printf("Failed to load texture\n");
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

		create_basic_srv(device);

		//Create rtv if we can be a render target
		if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
		{
			create_rtv(device);
		}
	}

	//TODO: Should likely be separated from TextureResource struct
	//Creates a basic SRV for this texture resource
	//TODO: RTV
	void create_basic_srv(const ComPtr<ID3D12Device> device)
	{
		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = 1;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&texture_descriptor_heap_srv)));
		texture_descriptor_heap_srv->SetName(TEXT("texture_descriptor_heap_srv"));

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

	void create_cubemap_srv(const ComPtr<ID3D12Device> device)
	{
		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = 1;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&cubemap_descriptor_heap_srv)));
		cubemap_descriptor_heap_srv->SetName(TEXT("cubemap_descriptor_heap_srv"));

		const UINT16 texture_array_size = texture_resource->GetDesc().DepthOrArraySize;
		assert(texture_array_size >= 6);
		
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		
		//Shared State
		srv_desc.Format = texture_resource->GetDesc().Format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.TextureCube.MostDetailedMip = 0;
		srv_desc.TextureCube.MipLevels = 1;
		srv_desc.TextureCube.ResourceMinLODClamp = 0.f;
		
		device->CreateShaderResourceView(texture_resource.Get(), &srv_desc, cubemap_descriptor_heap_srv->GetCPUDescriptorHandleForHeapStart());
	}

	void create_rtv(const ComPtr<ID3D12Device> device)
	{
		D3D12_RESOURCE_DESC resource_desc = texture_resource->GetDesc();
		assert(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		
		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = resource_desc.DepthOrArraySize;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&texture_descriptor_heap_rtv)));
		texture_descriptor_heap_rtv->SetName(TEXT("texture_descriptor_heap_rtv"));
		
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

struct GraphicsPipelineBuilder
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;

	ComPtr<ID3DBlob> vs_bytecode;
	ComPtr<ID3DBlob> ps_bytecode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> input_element_descs;

	GraphicsPipelineBuilder()
	{
		pso_desc = {};
		
		pso_desc.RasterizerState = {};
		pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
		pso_desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		pso_desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		pso_desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		pso_desc.RasterizerState.DepthClipEnable = TRUE;
		pso_desc.RasterizerState.MultisampleEnable = FALSE;
		pso_desc.RasterizerState.AntialiasedLineEnable = FALSE;
		pso_desc.RasterizerState.ForcedSampleCount = 0;
		pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		pso_desc.DepthStencilState.DepthEnable = TRUE;
		pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pso_desc.DepthStencilState.StencilEnable = FALSE;

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

		pso_desc.SampleMask = UINT_MAX;

		pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pso_desc.SampleDesc.Count = 1;
		// pso_desc.SampleDesc.Quality = 0;
	}

	GraphicsPipelineBuilder& with_root_signature(const ComPtr<ID3D12RootSignature> in_root_signature)
	{
		pso_desc.pRootSignature = in_root_signature.Get();
		return *this;
	}

	GraphicsPipelineBuilder& with_vs(const ComPtr<ID3DBlob> in_shader_bytecode)
	{
		vs_bytecode = in_shader_bytecode;
		pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs_bytecode.Get());
		return *this;
	}

	GraphicsPipelineBuilder& with_ps(const ComPtr<ID3DBlob> in_shader_bytecode)
	{
		ps_bytecode = in_shader_bytecode;
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps_bytecode.Get());
		return *this;
	}

	GraphicsPipelineBuilder& with_rtv_formats(std::vector<DXGI_FORMAT> rtv_formats)
	{
		assert(rtv_formats.size() <= 8);
		
		D3D12_RT_FORMAT_ARRAY rtv_format_array = {};
		rtv_format_array.NumRenderTargets = static_cast<UINT>(rtv_formats.size());
		for (UINT i = 0; i < rtv_format_array.NumRenderTargets; ++i)
		{
			rtv_format_array.RTFormats[i] = rtv_formats[i];
		}
		return *this;
	}

	GraphicsPipelineBuilder& with_dsv_format(const DXGI_FORMAT in_format)
	{
		pso_desc.DSVFormat = in_format;
		return *this;
	}

	GraphicsPipelineBuilder& with_depth_enabled(const bool in_enable_depth)
	{
		pso_desc.DepthStencilState.DepthEnable = in_enable_depth;
		return *this;
	}

	GraphicsPipelineBuilder& with_primitive_topology(const D3D12_PRIMITIVE_TOPOLOGY_TYPE in_primitive_topology)
	{
		pso_desc.PrimitiveTopologyType = in_primitive_topology;
		return *this;
	}

	// GraphicsPipelineBuilder& with_input_layout(const std::vector<D3D12_INPUT_ELEMENT_DESC> in_element_descs)
	// {
	// 	input_element_descs = in_element_descs;
	// 	pso_desc.InputLayout.NumElements = input_element_descs.size();
	// 	pso_desc.InputLayout.pInputElementDescs = input_element_descs.data();
	// 	return *this;
	// }

	//TODO: More rasterizer setters

	GraphicsPipelineBuilder& with_cull_mode(const D3D12_CULL_MODE in_cull_mode)
	{
		pso_desc.RasterizerState.CullMode = in_cull_mode;
		return *this;
	}

	static DXGI_FORMAT get_format_from_parameter_reflection(const D3D12_SIGNATURE_PARAMETER_DESC& in_reflection)
	{
		if (in_reflection.Mask == 1)
		{
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  { return DXGI_FORMAT_R32_UINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  { return DXGI_FORMAT_R32_SINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) { return DXGI_FORMAT_R32_FLOAT; }
		}
		else if (in_reflection.Mask <= 3)
		{
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  { return DXGI_FORMAT_R32G32_UINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  { return DXGI_FORMAT_R32G32_SINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) { return DXGI_FORMAT_R32G32_FLOAT; }
		}
		else if (in_reflection.Mask <= 7)
		{
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  { return DXGI_FORMAT_R32G32B32_UINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  { return DXGI_FORMAT_R32G32B32_SINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) { return DXGI_FORMAT_R32G32B32_FLOAT; }
		}
		else if (in_reflection.Mask <= 15)
		{
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  { return DXGI_FORMAT_R32G32B32A32_UINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_SINT32)  { return DXGI_FORMAT_R32G32B32A32_SINT;  }
			if (in_reflection.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) { return DXGI_FORMAT_R32G32B32A32_FLOAT; }
		}

		return DXGI_FORMAT_UNKNOWN;
	}

	ComPtr<ID3D12PipelineState> build(ComPtr<ID3D12Device> device)
	{
		assert(pso_desc.pRootSignature != nullptr);
		assert(pso_desc.VS.pShaderBytecode != nullptr);
		assert(pso_desc.PS.pShaderBytecode != nullptr);
		
		ID3D12ShaderReflection* vertex_shader_reflection = nullptr;
		HR_CHECK(D3DReflect(vs_bytecode->GetBufferPointer(), vs_bytecode->GetBufferSize(), IID_PPV_ARGS(&vertex_shader_reflection)));

		D3D12_SHADER_DESC vertex_shader_desc;
		HR_CHECK(vertex_shader_reflection->GetDesc(&vertex_shader_desc));

		//Set InputLayout from vertex-shader inputs
		input_element_descs.clear();
		input_element_descs.reserve(vertex_shader_desc.InputParameters);
		for (UINT i = 0; i < vertex_shader_desc.InputParameters; ++i)
		{
			D3D12_SIGNATURE_PARAMETER_DESC input_element_reflection;
			HR_CHECK(vertex_shader_reflection->GetInputParameterDesc(i, &input_element_reflection));

			D3D12_INPUT_ELEMENT_DESC input_element_desc = {};
			input_element_desc.SemanticName = input_element_reflection.SemanticName;
			input_element_desc.SemanticIndex = input_element_reflection.SemanticIndex;
			input_element_desc.Format = get_format_from_parameter_reflection(input_element_reflection);
			input_element_desc.InputSlot = 0; //TODO?
			input_element_desc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
			input_element_desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			input_element_desc.InstanceDataStepRate = 0;
			input_element_descs.push_back(input_element_desc);
		}

		pso_desc.InputLayout.NumElements = input_element_descs.size();
		pso_desc.InputLayout.pInputElementDescs = input_element_descs.data();

		D3D12_SIGNATURE_PARAMETER_DESC input_parameter_desc;
		HR_CHECK(vertex_shader_reflection->GetInputParameterDesc(0, &input_parameter_desc));
	
		ComPtr<ID3D12PipelineState> out_pipeline_state;
		HR_CHECK(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&out_pipeline_state)));
		return out_pipeline_state;
	}
};