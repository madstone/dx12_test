#include <iostream>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <cassert>
#include <vector>

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
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list));
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
		.Flags = D3D12_RESOURCE_FLAG_NONE,
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
		D3D12_HEAP_FLAG_NONE,
		&texture_desc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture));
	assert(SUCCEEDED(hr));

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT buffer_footprint = {};
	UINT64 texture_size = 0;
	device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &buffer_footprint, nullptr, nullptr, &texture_size);


	constexpr D3D12_HEAP_PROPERTIES upload_heap_prop = {
		.Type = D3D12_HEAP_TYPE_UPLOAD,
		.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
		.CreationNodeMask = 1,
		.VisibleNodeMask = 1
	};

	D3D12_RESOURCE_DESC upload_texture_desc = {
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

	ComPtr<ID3D12Resource> upload_texture;
	hr = device->CreateCommittedResource(
		&upload_heap_prop,
		D3D12_HEAP_FLAG_NONE,
		&upload_texture_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&upload_texture));
	assert(SUCCEEDED(hr));

	UINT8* ptr;
	D3D12_RANGE upload_range{ 0, upload_texture_desc.Width };
	hr = upload_texture->Map(0, &upload_range, reinterpret_cast<void**>(&ptr));
	assert(SUCCEEDED(hr));
	std::memset(ptr, 0xff, texture_size);
	upload_texture->Unmap(0, nullptr);

	D3D12_TEXTURE_COPY_LOCATION src_location = {
		.pResource = upload_texture.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
		.PlacedFootprint = buffer_footprint
	};

	D3D12_TEXTURE_COPY_LOCATION dst_location = {
		.pResource = texture.Get(),
		.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0
	};

	command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);

	D3D12_RESOURCE_BARRIER barrier = {
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
		.Transition = {
			.pResource = texture.Get(),
			.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
			.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		}
	};
	command_list->ResourceBarrier(1, &barrier);

    std::cout << "Hello World!\n";
}
