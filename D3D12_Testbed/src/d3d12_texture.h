#pragma once

#include <wrl.h>
using Microsoft::WRL::ComPtr;

#include <mutex>

#include <d3d12.h>
#include <D3Dcompiler.h>
#include "D3D12MemAlloc/D3D12MemAlloc.h"
#include "d3dx12.h"

#include <cstdio>
#include <cassert>

#include <EASTL/optional.h>
using eastl::optional;

//STB Image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Remotery/Remotery.h"

#include "d3d12_helpers.h"

constexpr int BINDLESS_INVALID_INDEX = -1;

struct Texture
{
	ComPtr<ID3D12Resource> resource;
	D3D12MA::Allocation* allocation = nullptr;

	int bindless_index = BINDLESS_INVALID_INDEX;
	bool is_cubemap = false;

	//TODO: this likely shouldn't be managed by the texture resource
	ComPtr<ID3D12DescriptorHeap> texture_descriptor_heap_rtv;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;

	//TODO: Temp
	ComPtr<ID3D12DescriptorHeap> per_mip_texture_descriptor_heap_rtv;
	std::vector<std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>> per_mip_rtv_handles;

	std::string debug_name;

	Texture(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const D3D12MA::ALLOCATION_DESC& texture_alloc_desc, const D3D12_RESOURCE_DESC& resource_desc)
	{
		//FCS TODO: Do this in TextureBuilder?
		const bool can_have_clear_value = (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
									   || (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		D3D12_CLEAR_VALUE clear_value = {};
		clear_value.Format = resource_desc.Format;

		HR_CHECK(gpu_memory_allocator->CreateResource(
			&texture_alloc_desc,
			&resource_desc,
			D3D12_RESOURCE_STATE_COMMON,
			can_have_clear_value ? &clear_value : nullptr,
			&allocation,
			IID_PPV_ARGS(&resource)
		));

		if (resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
		{
			create_rtv(device);
		}
	}

	void create_texture(const ComPtr<ID3D12Device> device,
						D3D12MA::Allocator* gpu_memory_allocator,
						const DXGI_FORMAT format,
						const UINT mip_levels,
						const D3D12_RESOURCE_FLAGS flags,
						const UINT image_width,
						const UINT image_height,
						const UINT image_count)
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
		texture_desc.MipLevels = mip_levels;
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
			&allocation,
			IID_PPV_ARGS(&resource)
		));
		
		//Create rtv if we can be a render target
		if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
		{
			create_rtv(device);
		}
	}

	template <typename T>
	void upload_texture_data(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const ComPtr<ID3D12CommandQueue> command_queue, const int desired_channels, const T* image_data, const int image_width, const int image_height) const
	{
		const size_t image_pixel_size = desired_channels * sizeof(T);

			ComPtr<ID3D12Resource> staging_buffer;
			D3D12MA::Allocation* staging_buffer_allocation = nullptr;

			//Create staging buffer
			D3D12MA::ALLOCATION_DESC staging_buffer_alloc_desc = {};
			staging_buffer_alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC staging_buffer_desc = {};
			staging_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			staging_buffer_desc.Alignment = 0;
			staging_buffer_desc.Width = GetRequiredIntermediateSize(resource.Get(), 0, 1); //Important
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

			UpdateSubresources(command_list.Get(), resource.Get(), staging_buffer.Get(), 0, 0, 1, &subresource_data);
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			command_list->ResourceBarrier(1, &barrier);

			command_list->Close();

			ID3D12CommandList* p_cmd_list = command_list.Get();
			command_queue->ExecuteCommandLists(1, &p_cmd_list);

			wait_gpu_idle(device, command_queue);

			staging_buffer_allocation->Release();
	}

	//FCS TODO: Remove this, add "register cubemap texture" to bindless_resource_manager, which will check for 6 array elements
	void set_is_cubemap(const bool new_is_cubemap)
	{
		is_cubemap = new_is_cubemap;
		if (is_cubemap)
		{
			const UINT16 texture_array_size = resource->GetDesc().DepthOrArraySize;
			assert(texture_array_size >= 6);
		}
	}

	//TODO: Handle this outside of texture struct
	void create_rtv(const ComPtr<ID3D12Device> device)
	{
		const UINT rtv_heap_offset = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		
		D3D12_RESOURCE_DESC resource_desc = resource->GetDesc();
		assert(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

		//create texture descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.NumDescriptors = resource_desc.DepthOrArraySize;
		// This heap contains SRV, UAV or CBVs -- in our case one SRV
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptor_heap_desc.NodeMask = 0;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		
		{
			HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&texture_descriptor_heap_rtv)));
			texture_descriptor_heap_rtv->SetName(TEXT("texture_descriptor_heap_rtv"));
			
			D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor_handle = texture_descriptor_heap_rtv->GetCPUDescriptorHandleForHeapStart();

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
				device->CreateRenderTargetView(resource.Get(), &rtv_desc, rtv_descriptor_handle);
				rtv_handles.push_back(rtv_descriptor_handle);
				rtv_descriptor_handle.ptr += rtv_heap_offset;
			}
		}

		//TODO: Testing per mip rtv views
		{
			descriptor_heap_desc.NumDescriptors = resource_desc.DepthOrArraySize * resource_desc.MipLevels;
			HR_CHECK(device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&per_mip_texture_descriptor_heap_rtv)));
			per_mip_texture_descriptor_heap_rtv->SetName(TEXT("per_mip_texture_descriptor_heap_rtv"));
			
			D3D12_CPU_DESCRIPTOR_HANDLE per_mip_rtv_descriptor_handle = per_mip_texture_descriptor_heap_rtv->GetCPUDescriptorHandleForHeapStart();

			const UINT16 texture_array_size = resource_desc.DepthOrArraySize;
			const bool is_texture_array = texture_array_size > 1;

			D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};

			//Shared State
			rtv_desc.Format = resource_desc.Format;
			rtv_desc.ViewDimension = is_texture_array ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;

			for (UINT mip_level = 0; mip_level < resource_desc.MipLevels; ++mip_level)
			{
				per_mip_rtv_handles.push_back({});
				
				for (UINT i = 0; i < resource_desc.DepthOrArraySize; ++i)
				{
					if (is_texture_array)
					{
						//Treating texture array RTVs as individual 'views' into each slice
						rtv_desc.Texture2DArray.MipSlice = mip_level;
						rtv_desc.Texture2DArray.FirstArraySlice = i;
						rtv_desc.Texture2DArray.ArraySize = 1;
						rtv_desc.Texture2DArray.PlaneSlice = 0;
					}
					else
					{
						rtv_desc.Texture2D.MipSlice = mip_level;
						rtv_desc.Texture2D.PlaneSlice = 0;
					}
					device->CreateRenderTargetView(resource.Get(), &rtv_desc, per_mip_rtv_descriptor_handle);
					per_mip_rtv_handles[mip_level].push_back(per_mip_rtv_descriptor_handle);
					per_mip_rtv_descriptor_handle.ptr += rtv_heap_offset;
				}
			}
		}
	}

	void set_name(const char* in_name)
	{
		assert(resource);
		debug_name = in_name;
		
		const std::wstring wide_str = std::wstring(debug_name.begin(), debug_name.end());
		resource->SetName(wide_str.c_str());
	}

	const char* get_name() const
	{
		return debug_name.c_str();
	}

	void release()
	{
		if (allocation)
		{
			allocation->Release();
			allocation = nullptr;
		}
	}
};

struct TextureBuilder
{
	D3D12MA::ALLOCATION_DESC texture_alloc_desc = {};
	D3D12_RESOURCE_DESC texture_desc = {};

	std::string debug_name;
	
	std::vector<uint8_t> binary_file_data;
	bool flip_vertically_on_load = false;
	
	TextureBuilder()
	{
		texture_alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		texture_desc.DepthOrArraySize = 1;
		texture_desc.MipLevels = 1;
		texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; //FCS TODO: also a fn for this
		texture_desc.Alignment = 0; //FCS TODO: also fn for this
		texture_desc.SampleDesc.Count = 1; //FCS TODO: also fn for this
		texture_desc.SampleDesc.Quality = 0; //FCS TODO: also fn for this
		texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; //FCS TODO: also fn for this
	}

	TextureBuilder& with_width(const UINT in_width)
	{
		texture_desc.Width = in_width;
		return *this;
	}

	TextureBuilder& with_height(const UINT in_height)
	{
		texture_desc.Height = in_height;
		return *this;
	}

	TextureBuilder& with_array_size(const UINT16 in_array_size)
	{
		texture_desc.DepthOrArraySize = in_array_size;
		return *this;
	}

	TextureBuilder& with_mip_levels(const UINT16 in_mip_levels)
	{
		texture_desc.MipLevels = in_mip_levels;
		return *this;
	}

	TextureBuilder& with_format(const DXGI_FORMAT in_format)
	{
		texture_desc.Format = in_format;
		return *this;
	}

	TextureBuilder& with_resource_flags(const D3D12_RESOURCE_FLAGS in_resource_flags)
	{
		texture_desc.Flags = in_resource_flags;
		return *this;
	}

	TextureBuilder& with_debug_name(const char* in_debug_name)
	{
		debug_name = in_debug_name;
		return *this;
	}

	TextureBuilder& from_file(const char* in_file)
	{
		if (FILE *fp = open_binary_file(in_file)) //See gltf.h
		{
			binary_file_data.clear();
			//Go to the end of the file. 
			if (fseek(fp, 0L, SEEK_END) == 0)
			{
				const long buffer_size = ftell(fp);
				if (buffer_size > 0)
				{
					binary_file_data.resize(buffer_size);
					//Go back to the start of the file. 
					if (fseek(fp, 0L, SEEK_SET) != 0) { /* Error */ }

					//Read the entire file into memory. 
					fread(binary_file_data.data(), sizeof(char), buffer_size, fp);
					if (ferror(fp) != 0)
					{
						printf("Error Reading File: %s\n", in_file);
					}
				}		
			}
			fclose(fp);
		}
		return* this;
	}

	TextureBuilder& from_binary_data(uint8_t* buffer, const size_t buffer_len)
	{
		binary_file_data.clear();
		binary_file_data.assign(buffer, buffer + buffer_len);
		return *this;
	}

	TextureBuilder& flip_vertically(const bool in_flip_vertically_on_load)
	{
		flip_vertically_on_load = in_flip_vertically_on_load;
		return *this;
	}

	//TODO: from_file and from_binary_data should take in required GPU objects and store refs to them?
	
	Texture build(const ComPtr<ID3D12Device> device, D3D12MA::Allocator* gpu_memory_allocator, const ComPtr<ID3D12CommandQueue> command_queue)
	{
		rmt_ScopedCPUSample(TextureBuilder_build, 0);
		if (!binary_file_data.empty() && command_queue != nullptr)
		{
			const int32_t required_components = 4;
			stbi_set_flip_vertically_on_load(flip_vertically_on_load);
			
			const bool is_file_hdr = stbi_is_hdr_from_memory(binary_file_data.data(), static_cast<int>(binary_file_data.size()));
			if (is_file_hdr)
			{
				int image_width, image_height, image_components;
				if (float* image_data = stbi_loadf_from_memory(binary_file_data.data(), static_cast<int>(binary_file_data.size()), &image_width, &image_height, &image_components, required_components))
				{
					with_width(image_width);
					with_height(image_height);
					with_format(DXGI_FORMAT_R32G32B32A32_FLOAT); //FCS TODO: Don't assume format

					//FCS TODO: BEGIN DUPLICATE CODE
					Texture out_texture(device, gpu_memory_allocator, texture_alloc_desc, texture_desc);

					if (!debug_name.empty())
					{
						out_texture.set_name(debug_name.c_str());
					}
					//FCS TODO: END DUPLICATE CODE

					out_texture.upload_texture_data(device, gpu_memory_allocator, command_queue, required_components, image_data, image_width, image_height);
		
					stbi_image_free(image_data);

					return out_texture; //FCS TODO: Unify returns
				}
			}
			else
			{
				int image_width, image_height, image_components;
				if (stbi_uc* image_data = stbi_load_from_memory(binary_file_data.data(), static_cast<int>(binary_file_data.size()), &image_width, &image_height, &image_components, required_components))
				{
					with_width(image_width);
					with_height(image_height);
					with_format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB); //FCS TODO: Don't assume format

					//FCS TODO: BEGIN DUPLICATE CODE
					Texture out_texture(device, gpu_memory_allocator, texture_alloc_desc, texture_desc);

					if (!debug_name.empty())
					{
						out_texture.set_name(debug_name.c_str());
					}
					//FCS TODO: END DUPLICATE CODE

					out_texture.upload_texture_data(device, gpu_memory_allocator, command_queue, required_components, image_data, image_width, image_height);
		
					stbi_image_free(image_data);

					return out_texture; //FCS TODO: Unify returns
				}
			}
		}

		//FCS TODO: BEGIN DUPLICATE CODE
		Texture out_texture(device, gpu_memory_allocator, texture_alloc_desc, texture_desc);

		if (!debug_name.empty())
		{
			out_texture.set_name(debug_name.c_str());
		}
		//FCS TODO: END DUPLICATE CODE

		return out_texture; //FCS TODO: Unify returns
	}
};

//FCS TODO: bindless samplers

constexpr UINT BINDLESS_TABLE_SIZE		   = 10000;
constexpr UINT BINDLESS_DESC_TYPES		   = 2;
constexpr UINT TEXTURE_2D_REGISTER_SPACE   = 1;
constexpr UINT TEXTURE_CUBE_REGISTER_SPACE = 2;

struct BindlessResourceManager
{
	ComPtr<ID3D12Device> device;

	ComPtr<ID3D12DescriptorHeap> bindless_descriptor_heap;

	optional<Texture> invalid_texture;
	optional<Texture> invalid_cubemap;

	std::vector<size_t> texture_free_indices;
	size_t texture_current_size = 0;

	std::vector<size_t> cubemap_free_indices;
	size_t cubemap_current_size = 0;

	UINT cbv_srv_uav_heap_offset;

	std::mutex manager_mutex;

	BindlessResourceManager(const ComPtr<ID3D12Device> in_device, D3D12MA::Allocator* gpu_memory_allocator)
		: device(in_device)
		, cbv_srv_uav_heap_offset(device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
	{
		//FCS TODO: Eventually this could hold other bindless resources as well (CBVs, for example)

		//TODO: Load some meaningful images to denote indexing errors
		invalid_texture = TextureBuilder()
			.with_format(DXGI_FORMAT_R32G32B32A32_FLOAT)
			.with_width(4)
			.with_height(4)
			.with_debug_name("Invalid Texture")
			.build(device, gpu_memory_allocator, nullptr);

		invalid_cubemap = TextureBuilder()
			.with_format(DXGI_FORMAT_R32G32B32A32_FLOAT)
			.with_width(4)
			.with_height(4)
			.with_array_size(6)
			.with_debug_name("Invalid Cubemap")
			.build(device, gpu_memory_allocator, nullptr);
		
		invalid_cubemap->set_is_cubemap(true);

		D3D12_DESCRIPTOR_HEAP_DESC bindless_heap_desc = {};
		bindless_heap_desc.NumDescriptors = BINDLESS_TABLE_SIZE * BINDLESS_DESC_TYPES;
		bindless_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		bindless_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HR_CHECK(device->CreateDescriptorHeap(&bindless_heap_desc, IID_PPV_ARGS(&bindless_descriptor_heap)));
		bindless_descriptor_heap->SetName(TEXT("bindless_descriptor_heap"));

		//Init unfilled slots with dummy resource so we don't index into invalid data
		for (UINT index = 0; index < BINDLESS_TABLE_SIZE; ++index)
		{
			create_srv_at_index(*invalid_texture, index);
			create_srv_at_index(*invalid_cubemap, index);
		}
	}

	void release()
	{
		invalid_texture->release();
		invalid_cubemap->release();

		//TODO: Need to unset all textures' bindless indices
	}

	void create_srv_at_index(Texture& in_texture, const size_t index) const
	{
		const bool is_cubemap = in_texture.is_cubemap;
		const D3D12_RESOURCE_DESC resource_desc = in_texture.resource->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};

		//Shared State
		srv_desc.Format = in_texture.resource->GetDesc().Format;
		srv_desc.ViewDimension = is_cubemap ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		if (is_cubemap)
		{
			srv_desc.TextureCube.MostDetailedMip = 0;
			srv_desc.TextureCube.MipLevels = resource_desc.MipLevels;
			srv_desc.TextureCube.ResourceMinLODClamp = 0.f;
		}
		else
		{
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Texture2D.MipLevels = resource_desc.MipLevels;
			srv_desc.Texture2D.PlaneSlice = 0;
			srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		const size_t heap_index = is_cubemap ? index + BINDLESS_TABLE_SIZE : index;
		UINT heap_offset = static_cast<UINT>(heap_index * cbv_srv_uav_heap_offset);

		D3D12_CPU_DESCRIPTOR_HANDLE descriptor_handle = bindless_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		descriptor_handle.ptr += heap_offset;
		device->CreateShaderResourceView(in_texture.resource.Get(), &srv_desc, descriptor_handle);

		in_texture.bindless_index = static_cast<int>(index);
	}

	void register_texture(Texture& in_texture_resource)
	{
		std::scoped_lock lock(manager_mutex);
		
		const bool is_cubemap = in_texture_resource.is_cubemap;

		if (in_texture_resource.bindless_index != BINDLESS_INVALID_INDEX)
		{
			printf("Error: Texture is already bound\n");
			return;
		}

		size_t index = BINDLESS_INVALID_INDEX;
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

		if (index != BINDLESS_INVALID_INDEX)
		{
			create_srv_at_index(in_texture_resource, index);
		}
	}

	void unregister_texture(Texture& in_texture_resource)
	{
		std::scoped_lock lock(manager_mutex);
		
		if (in_texture_resource.bindless_index != BINDLESS_INVALID_INDEX)
		{
			const size_t index = in_texture_resource.bindless_index;

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

			in_texture_resource.bindless_index = BINDLESS_INVALID_INDEX;

			Texture& invalid_resource = is_cubemap ? *invalid_cubemap : *invalid_texture;
			create_srv_at_index(invalid_resource, index);
		}
	}

	D3D12_GPU_DESCRIPTOR_HANDLE get_texture_gpu_handle() const
	{
		return bindless_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE get_cubemap_gpu_handle() const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE out_handle = bindless_descriptor_heap->GetGPUDescriptorHandleForHeapStart();
		const UINT cubemap_offset = BINDLESS_TABLE_SIZE * cbv_srv_uav_heap_offset;
		out_handle.ptr += cubemap_offset;
		return out_handle;
	}
};

// TODO: Tests for BindlessResourceManager (registering/unregistering)