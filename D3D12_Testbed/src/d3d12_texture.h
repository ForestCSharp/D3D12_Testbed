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

#include "d3d12_helpers.h"

//TODO: TextureBuilder:
/* texture_builder
.width(4)
.height(4)
.format(...)
.array_count(6)
.allow_render_target(true)
.build();
*/

#define INVALID_INDEX -1

struct TextureResource
{
	ComPtr<ID3D12Resource> texture_resource;
	D3D12MA::Allocation* texture_allocation = nullptr;

	int bindless_index = INVALID_INDEX;

	//TODO: Won't need these heaps when we go bindless
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_srv;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;

	ComPtr<ID3D12DescriptorHeap> cubemap_descriptor_heap_srv;
	bool is_cubemap = false;

	//TODO: this likely shouldn't be managed by the texture resource
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_rtv;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;

	TextureResource(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const DXGI_FORMAT format, const D3D12_RESOURCE_FLAGS flags, const UINT image_width, const UINT image_height, const UINT image_count)
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
		if (float* image_data = stbi_loadf(file, &image_width, &image_height, nullptr, desired_channels))
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
			staging_buffer_desc.Width = GetRequiredIntermediateSize(texture_resource.Get(), 0, 1); //Important
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

		srv_desc = {};

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

		srv_desc.Format = texture_resource->GetDesc().Format;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.TextureCube.MostDetailedMip = 0;
		srv_desc.TextureCube.MipLevels = 1;
		srv_desc.TextureCube.ResourceMinLODClamp = 0.f;

		device->CreateShaderResourceView(texture_resource.Get(), &srv_desc, cubemap_descriptor_heap_srv->GetCPUDescriptorHandleForHeapStart());
		is_cubemap = true;
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

constexpr UINT BINDLESS_TABLE_SIZE		   = 10000;
constexpr UINT BINDLESS_DESC_TYPES		   = 2;
constexpr UINT TEXTURE_2D_REGISTER_SPACE   = 1;
constexpr UINT TEXTURE_CUBE_REGISTER_SPACE = 2;

struct BindlessTextureManager
{
	ComPtr<ID3D12Device> device;

	ComPtr<ID3D12DescriptorHeap> bindless_descriptor_heap;

	TextureResource invalid_texture;
	TextureResource invalid_cubemap;

	std::vector<size_t> texture_free_indices;
	size_t texture_current_size = 0;

	std::vector<size_t> cubemap_free_indices;
	size_t cubemap_current_size = 0;

	UINT cbv_srv_uav_heap_offset;

	BindlessTextureManager(ComPtr<ID3D12Device> in_device, D3D12MA::Allocator* gpu_memory_allocator)
		: device(in_device)
		, invalid_texture(device, gpu_memory_allocator, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE, 4, 4, 1) //TODO: Load some meaningful image to denote indexing errors
		, invalid_cubemap(device, gpu_memory_allocator, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_NONE, 4, 4, 6) //TODO: Load some meaningful image to denote indexing errors
		, cbv_srv_uav_heap_offset(device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
	{
		//FCS TODO: Eventually this could hold other data as well (CBVs, for example)
		invalid_cubemap.create_cubemap_srv(device);

		invalid_texture.set_name(TEXT("Invalid Texture"));

		D3D12_DESCRIPTOR_HEAP_DESC bindless_heap_desc = {};
		bindless_heap_desc.NumDescriptors = BINDLESS_TABLE_SIZE * BINDLESS_DESC_TYPES;
		bindless_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HR_CHECK(device->CreateDescriptorHeap(&bindless_heap_desc, IID_PPV_ARGS(&bindless_descriptor_heap)));
		bindless_descriptor_heap->SetName(TEXT("bindless_descriptor_heap"));

		//Init unfilled slots with dummy resource so we don't index into invalid data
		for (UINT index = 0; index < BINDLESS_TABLE_SIZE; ++index)
		{
			create_srv_at_index(invalid_texture, index);
			create_srv_at_index(invalid_cubemap, index);
		}
	}

	void release()
	{
		invalid_texture.release();
		invalid_cubemap.release();

		//TODO: Need to unset all textures bindless indices
	}

	void create_srv_at_index(TextureResource& in_texture_resource, size_t index)
	{
		const bool is_cubemap = in_texture_resource.is_cubemap;

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};

		//Shared State
		srv_desc.Format = in_texture_resource.texture_resource->GetDesc().Format;
		srv_desc.ViewDimension = is_cubemap ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		if (is_cubemap)
		{
			srv_desc.TextureCube.MostDetailedMip = 0;
			srv_desc.TextureCube.MipLevels = 1;
			srv_desc.TextureCube.ResourceMinLODClamp = 0.f;
		}
		else
		{
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = 1;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		if (is_cubemap)
		{
			index += BINDLESS_TABLE_SIZE;
		}
		UINT heap_offset = index * cbv_srv_uav_heap_offset;

		D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle = bindless_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		descriptor_handle.ptr += heap_offset;
		device->CreateShaderResourceView(in_texture_resource.texture_resource.Get(), &srv_desc, descriptor_handle);

		in_texture_resource.bindless_index = index;
	}

	void register_texture(TextureResource& in_texture_resource)
	{
		bool is_cubemap = in_texture_resource.is_cubemap;

		if (in_texture_resource.bindless_index != INVALID_INDEX)
		{
			printf("Error: Texture is already bound\n");
			return;
		}

		size_t index = INVALID_INDEX;
		std::vector<size_t>& free_list_ref = is_cubemap ? cubemap_free_indices : texture_free_indices;
		size_t& current_size_ref = is_cubemap ? cubemap_current_size : texture_current_size;

		//Find first free-list entry
		if (free_list_ref.size() > 0)
		{
			index = free_list_ref[0];
			free_list_ref.erase(free_list_ref.begin());
			printf("Registered from free list. Index: %llu\n", index);
		}
		else if (current_size_ref < BINDLESS_TABLE_SIZE)
		{
			index = current_size_ref++;
			printf("Registered on end. Index: %llu\n", index);
		}
		else
		{
			//Ran out of slots
		}

		if (index != INVALID_INDEX)
		{
			create_srv_at_index(in_texture_resource, index);
		}
	}

	void unregister_texture(TextureResource& in_texture_resource)
	{

		if (in_texture_resource.bindless_index != INVALID_INDEX)
		{
			size_t index = in_texture_resource.bindless_index;

			const bool is_cubemap = in_texture_resource.is_cubemap;
			std::vector<size_t>& free_list_ref = is_cubemap ? cubemap_free_indices : texture_free_indices;
			size_t& current_size_ref = is_cubemap ? cubemap_current_size : texture_current_size;

			assert(index < BINDLESS_TABLE_SIZE);
			assert(current_size_ref > 0);

			if (index == current_size_ref - 1)
			{
				current_size_ref--;
				printf("Freed from end\n");
			}
			else
			{
				free_list_ref.push_back(index); //Unsorted free list
				printf("Freed in middle\n");
			}

			in_texture_resource.bindless_index = INVALID_INDEX;

			//TODO: Clean up SRV?
			TextureResource& invalid_resource = is_cubemap ? invalid_cubemap : invalid_texture;
			create_srv_at_index(invalid_resource, index);
		}
	}

	D3D12_GPU_DESCRIPTOR_HANDLE get_texture_handle() const
	{
		return bindless_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE get_cubemap_handle() const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE out_handle = bindless_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
		UINT cubemap_offset = BINDLESS_TABLE_SIZE * cbv_srv_uav_heap_offset;
		out_handle.ptr += cubemap_offset;
		return out_handle;
	}
};