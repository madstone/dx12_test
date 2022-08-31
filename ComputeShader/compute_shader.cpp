// ComputeShader12.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include <iostream>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <cassert>
#include <vector>
#include <compute_shader.h>

using Microsoft::WRL::ComPtr;

ComPtr<IDXGIAdapter1> GetHardwareAdapter(ComPtr<IDXGIFactory6> factory)
{
	ComPtr<IDXGIAdapter1> adapter;
	for (UINT adapter_index = 0;
		SUCCEEDED(factory->EnumAdapterByGpuPreference(adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
		++adapter_index)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
			break;
	}
	return adapter;
}

int main()
{
	HRESULT hr;
	ComPtr<ID3D12Debug> debug;
	hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	assert(SUCCEEDED(hr));
	debug->EnableDebugLayer();

	UINT factory_flags = DXGI_CREATE_FACTORY_DEBUG;

	ComPtr<IDXGIFactory6> factory;
	hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory));
	assert(SUCCEEDED(hr));

	auto hardware_adapter = GetHardwareAdapter(factory);
	assert(hardware_adapter);

	ComPtr<ID3D12Device> device;
	hr = D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	assert(SUCCEEDED(hr));

	ComPtr<ID3D12InfoQueue> info_queue = nullptr;
	hr = device->QueryInterface(IID_PPV_ARGS(&info_queue));
	assert(SUCCEEDED(hr));
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

	ComPtr<ID3D12Fence> fence;
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {
		.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1
	};

	D3D12_DESCRIPTOR_RANGE1 ranges[1] = {
		{
			.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
			.NumDescriptors = 1,
			.BaseShaderRegister = 0,
			.RegisterSpace = 0,
			.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
			.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
		}
	};

	D3D12_ROOT_PARAMETER1 root_parameters[1] = {
		{
			.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
			.DescriptorTable = {
				.NumDescriptorRanges = 1,
				.pDescriptorRanges = ranges
			},
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
		}
	};

	D3D12_STATIC_SAMPLER_DESC samplers[1] = {
		{
			.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
			.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			.MipLODBias = 0,
			.MaxAnisotropy = 0,
			.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
			.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			.MinLOD = 0.0f,
			.MaxLOD = D3D12_FLOAT32_MAX,
			.ShaderRegister = 0,
			.RegisterSpace = 0,
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
		}
	};

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {
		.Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
		.Desc_1_1 = {
			.NumParameters = (UINT)std::size(root_parameters),
			.pParameters = root_parameters,
			.NumStaticSamplers = 1,
			.pStaticSamplers = samplers,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		}
	};

	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> error;
	hr = D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature_blob, &error);
	assert(SUCCEEDED(hr));
	
	ComPtr<ID3D12RootSignature> root_signature;
	hr = device->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
	assert(SUCCEEDED(hr));

	D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc = {
		.pRootSignature = root_signature.Get(),
		.CS = {
			.pShaderBytecode = g_compute_shader,
			.BytecodeLength = std::size(g_compute_shader)
		},
	};
	ComPtr<ID3D12PipelineState> pipeline_state;
	hr = device->CreateComputePipelineState(&compute_desc, IID_PPV_ARGS(&pipeline_state));
	assert(SUCCEEDED(hr));

	D3D12_COMMAND_QUEUE_DESC queue_desc = {
		.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
	};
	ComPtr<ID3D12CommandQueue> command_queue;
	hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

	ComPtr<ID3D12CommandAllocator> command_allocator;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
	assert(SUCCEEDED(hr));

	ComPtr<ID3D12GraphicsCommandList> command_list;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), pipeline_state.Get(), IID_PPV_ARGS(&command_list));
	assert(SUCCEEDED(hr));

	D3D12_RESOURCE_DESC texture_desc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Width = 4096,
		.Height = 4096,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
	};

	constexpr D3D12_HEAP_PROPERTIES heap_prop = {
		.Type = D3D12_HEAP_TYPE_DEFAULT,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1
	};

	ComPtr<ID3D12Resource> texture;
	hr = device->CreateCommittedResource(
		&heap_prop,
		D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
		&texture_desc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&texture));
	assert(SUCCEEDED(hr));

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
	UINT64 texture_size = 0;
	device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &bufferFootprint, nullptr, nullptr, &texture_size);

	constexpr D3D12_HEAP_PROPERTIES readback_heap_prop = {
		.Type = D3D12_HEAP_TYPE_READBACK,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1
	};

	D3D12_RESOURCE_DESC readback_desc = {
		.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width = texture_size,
		.Height = 1,
		.DepthOrArraySize = 1,
		.MipLevels = 1,
		.Format = DXGI_FORMAT_UNKNOWN,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		.Flags = D3D12_RESOURCE_FLAG_NONE,
	};

	ComPtr<ID3D12Resource> readback_texture;
	hr = device->CreateCommittedResource(
		&readback_heap_prop,
		D3D12_HEAP_FLAG_NONE,
		&readback_desc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&readback_texture));
	assert(SUCCEEDED(hr));

	ComPtr<ID3D12DescriptorHeap> uav;
	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = 1;
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&uav));
	assert(SUCCEEDED(hr));

	D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
		.Format = texture_desc.Format,
		.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MipSlice = 0,
			.PlaneSlice = 0
		}
	};
	device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav_desc, uav->GetCPUDescriptorHandleForHeapStart());

	command_list->SetPipelineState(pipeline_state.Get());
	command_list->SetComputeRootSignature(root_signature.Get());

	ID3D12DescriptorHeap* uav_heaps[] = { uav.Get() };
	command_list->SetDescriptorHeaps(_countof(uav_heaps), uav_heaps);

	auto uav_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	command_list->SetComputeRootDescriptorTable(0, uav->GetGPUDescriptorHandleForHeapStart());

	command_list->Dispatch(static_cast<UINT>(texture_desc.Width) / 32, texture_desc.Height / 32, 1);

	D3D12_RESOURCE_BARRIER barrier = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
		.Transition = {
			.pResource = texture.Get(),
			.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE
		}
	};
	command_list->ResourceBarrier(1, &barrier);
	
	D3D12_TEXTURE_COPY_LOCATION dst_location = {
		.pResource = readback_texture.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
		.PlacedFootprint = bufferFootprint
	};

	D3D12_TEXTURE_COPY_LOCATION src_location = {
		.pResource = texture.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0
	};

	command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);

	hr = command_list->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commmand_lists[] = { command_list.Get() };
	command_queue->ExecuteCommandLists(_countof(commmand_lists), commmand_lists);

	hr = command_queue->Signal(fence.Get(), 1);
	assert(SUCCEEDED(hr));

	auto completed_value = fence->GetCompletedValue();
	if (completed_value < 1)
	{
		auto event_handle = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		assert(event_handle != 0);
		fence->SetEventOnCompletion(1, event_handle);
		WaitForSingleObject(event_handle, INFINITE);
		CloseHandle(event_handle);
	}

	std::vector<UINT8> buffer;
	UINT8* ptr;
	D3D12_RANGE readback_range{ 0, texture_size };
	hr = readback_texture->Map(0, &readback_range, reinterpret_cast<void**>(&ptr));
	assert(SUCCEEDED(hr));
	buffer.assign(ptr, ptr + texture_size);
	readback_texture->Unmap(0, nullptr);
}
