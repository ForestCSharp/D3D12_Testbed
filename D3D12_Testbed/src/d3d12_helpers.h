#pragma once

#include <wrl.h>
using Microsoft::WRL::ComPtr;

#include <d3d12.h>
#include <D3Dcompiler.h>
#include "D3D12MemAlloc/D3D12MemAlloc.h"
#include "d3dx12.h"

#include "Remotery/Remotery.h"

#include <cstdio>
#include <cassert>

#define HR_CHECK(expr)  \
{\
HRESULT result = (expr);\
if (FAILED(result))\
{\
printf("FAILED HRESULT: Line: %i Code: %s Error: %x\n", __LINE__, #expr, result); \
exit(-1);\
}\
}\

#include <cstdlib>

// We do not intend to read from these resources on the CPU.
static const D3D12_RANGE no_read_range = { 0, 0 };

inline ComPtr<ID3DBlob> compile_shader(const LPCWSTR file_name, const LPCSTR entry_point, const LPCSTR target)
{
	rmt_ScopedCPUSample(compile_shader, 0);

	ComPtr<ID3DBlob> out_shader;

	wchar_t entry_point_wide[255];
	mbstowcs(entry_point_wide, entry_point, strlen(entry_point));
	std::wstring compiled_file_name = std::wstring(file_name) + L"." + entry_point_wide + L".ID3DBlob";

	//FCS TODO: Only recompile if missing (done) OR changed (TODO) (store file hash + blob in binary file)
	const bool file_missing = GetFileAttributes(compiled_file_name.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
	if (file_missing)
	{
		const UINT shader_compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

		ID3DBlob* error_messages;
		const HRESULT hr = D3DCompileFromFile(file_name, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, target, shader_compile_flags, 0, &out_shader, &error_messages);

		if (FAILED(hr) && error_messages)
		{
			const char* error_message = static_cast<const char*>(error_messages->GetBufferPointer());
			printf("CompileShader Error: %s\n", error_message);
		}

		D3DWriteBlobToFile(out_shader.Get(), compiled_file_name.c_str(), TRUE);
	}
	else
	{
		D3DReadFileToBlob(compiled_file_name.c_str(), &out_shader);
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

struct GpuRenderData
{
	ComPtr<ID3D12Resource> vertex_buffer;
	D3D12MA::Allocation* vertex_buffer_allocation = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	
	ComPtr<ID3D12Resource> index_buffer;
	D3D12MA::Allocation* index_buffer_allocation = nullptr;
	D3D12_INDEX_BUFFER_VIEW index_buffer_view;

	GpuRenderData()
	{
		vertex_buffer_view = {};
		index_buffer_view = {};
	}

	template <typename T>
	GpuRenderData(D3D12MA::Allocator* gpu_memory_allocator, std::vector<T> vertices, std::vector<UINT32> indices)
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
		
		// FCS TODO: Need to upload with upload heap (see: "UpdateSubresources", used in Texture)
		
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

struct GraphicsPipelineBuilder
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;

	ComPtr<ID3DBlob> vs_bytecode;
	ComPtr<ID3DBlob> ps_bytecode;
	std::vector<D3D12_INPUT_ELEMENT_DESC> input_element_descs;

	std::wstring debug_name;

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

	GraphicsPipelineBuilder& with_rtv_formats(const std::initializer_list<DXGI_FORMAT> in_rtv_formats)
	{
		assert(in_rtv_formats.size() <= 8);
		std::vector<DXGI_FORMAT> rtv_formats(in_rtv_formats);
		
		pso_desc.NumRenderTargets = static_cast<UINT>(rtv_formats.size());
		for (UINT i = 0; i < pso_desc.NumRenderTargets; ++i)
		{
			pso_desc.RTVFormats[i] = rtv_formats[i];
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

	GraphicsPipelineBuilder& with_cull_mode(const D3D12_CULL_MODE in_cull_mode)
	{
		pso_desc.RasterizerState.CullMode = in_cull_mode;
		return *this;
	}

	GraphicsPipelineBuilder& with_debug_name(std::wstring in_debug_name)
	{
		debug_name = in_debug_name;
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

		pso_desc.InputLayout.NumElements = static_cast<UINT>(input_element_descs.size());
		pso_desc.InputLayout.pInputElementDescs = input_element_descs.data();

		D3D12_SIGNATURE_PARAMETER_DESC input_parameter_desc;
		HR_CHECK(vertex_shader_reflection->GetInputParameterDesc(0, &input_parameter_desc));
	
		ComPtr<ID3D12PipelineState> out_pipeline_state;
		HR_CHECK(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&out_pipeline_state)));
		if (!debug_name.empty())
		{
			out_pipeline_state->SetName(debug_name.c_str());
		}
		return out_pipeline_state;
	}
};