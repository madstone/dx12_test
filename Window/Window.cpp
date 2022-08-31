// Window.cpp : 애플리케이션에 대한 진입점을 정의합니다.
//

#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN             // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <array>
#include <format>
#include <cassert>

using Microsoft::WRL::ComPtr;

ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> command_queue;
ComPtr<IDXGISwapChain3> swap_chain;
ComPtr<ID3D12DescriptorHeap> rtv_heap;
UINT rtv_descriptor_size = 0;
constexpr UINT frame_count = 2;
std::array<ComPtr<ID3D12Resource>, frame_count> render_targets;
std::array<ComPtr<ID3D12CommandAllocator>, frame_count> command_allocators;
ComPtr<ID3D12GraphicsCommandList> command_list;
UINT frame_index = 0;
std::array<UINT64, frame_count> fence_values = {};
ComPtr<ID3D12Fence> fence;
HANDLE fence_event;

void destroy_device()
{
    rtv_heap.Reset();
    swap_chain.Reset();
    command_queue.Reset();
    device.Reset();
}

void create_device(HWND hwnd)
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
    assert(adapter);

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12InfoQueue> info_queue = nullptr;
    hr = device->QueryInterface(IID_PPV_ARGS(&info_queue));
    assert(SUCCEEDED(hr));
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
    };
    hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));
    assert(SUCCEEDED(hr));

    RECT client_rect;
    ::GetClientRect(hwnd, &client_rect);
    const auto width = static_cast<UINT>(client_rect.right - client_rect.left);
    const auto height = static_cast<UINT>(client_rect.bottom - client_rect.top);
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {
        .Width = width,
        .Height = height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = frame_count,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD
    };
    ComPtr<IDXGISwapChain1> swap_chain1;
    hr = factory->CreateSwapChainForHwnd(
        command_queue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
        hwnd,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &swap_chain1);
    assert(SUCCEEDED(hr));

    hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    assert(SUCCEEDED(hr));

    hr = swap_chain1.As(&swap_chain);
    assert(SUCCEEDED(hr));

    frame_index = swap_chain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = frame_count,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
    };
    hr = device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));
    assert(SUCCEEDED(hr));

    rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());

    for (auto n = decltype(frame_count){0}; n < frame_count; n++)
    {
        hr = swap_chain->GetBuffer(n, IID_PPV_ARGS(&render_targets[n]));
        assert(SUCCEEDED(hr));
        device->CreateRenderTargetView(render_targets[n].Get(), nullptr, rtv_handle);
        rtv_handle.ptr += rtv_descriptor_size;
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators[n]));
        assert(SUCCEEDED(hr));
    }

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&command_list));
    assert(SUCCEEDED(hr));

    hr = command_list->Close();
    assert(SUCCEEDED(hr));

    hr = device->CreateFence(fence_values[0], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fence_values[0]++;

    fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fence_event != nullptr);
}

void render(HWND hwnd)
{
    HRESULT hr;
    hr = command_allocators[frame_index]->Reset();
    assert(SUCCEEDED(hr));
    hr = command_list->Reset(command_allocators[frame_index].Get(), nullptr);
    assert(SUCCEEDED(hr));

    RECT client_rect;
    ::GetClientRect(hwnd, &client_rect);
    D3D12_VIEWPORT viewport = {
        .Width = static_cast<float>(client_rect.right - client_rect.left),
        .Height = static_cast<float>(client_rect.bottom - client_rect.top),
        .MinDepth = D3D12_MIN_DEPTH,
        .MaxDepth = D3D12_MAX_DEPTH
    };
    command_list->RSSetViewports(1, &viewport);

    auto rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr += frame_index * rtv_descriptor_size;

    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
            .pResource = render_targets[frame_index].Get(),
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
            .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
        }
    };
    command_list->ResourceBarrier(1, &barrier);
    command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

    const float clear_color[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list->ResourceBarrier(1, &barrier);

    hr = command_list->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* command_lists[] = { command_list.Get() };
    command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

    hr = swap_chain->Present(1, 0); // GetCurrentBackBufferIndex 증가
    assert(SUCCEEDED(hr));

    const auto current_fence_value = fence_values[frame_index];
    //::OutputDebugStringA(std::format("Signal(fence, {0});\n", current_fence_value).c_str());
    hr = command_queue->Signal(fence.Get(), current_fence_value);
    assert(SUCCEEDED(hr));

    frame_index = swap_chain->GetCurrentBackBufferIndex();

    const auto completed_value = fence->GetCompletedValue();
    //::OutputDebugStringA(std::format("GetCompletedValue() = {0};\n", completed_value).c_str());
    if (completed_value < fence_values[frame_index]) // 펜스의 GetCompletedValue 값이 다음 프레임의 펜스 값보다 작다는 것은 다음 프레임의 present가 끝나지 않았다는 것이므로 기다린다.
    {
        hr = fence->SetEventOnCompletion(fence_values[frame_index], fence_event);
        assert(SUCCEEDED(hr));
        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
    }

    fence_values[frame_index] = current_fence_value + 1;
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE,
                     _In_ LPWSTR,
                     _In_ int nCmdShow)
{
    std::wstring class_name = L"dx12_test_window";
    WNDCLASSEXW wcex{
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = hInstance,
        .hIcon = LoadIcon(nullptr, IDI_APPLICATION),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszMenuName = nullptr,
        .lpszClassName = class_name.c_str(),
        .hIconSm = LoadIcon(nullptr, IDI_APPLICATION)
    };
    RegisterClassExW(&wcex);

    auto hwnd = CreateWindowExW(0L, class_name.c_str(), L"dx12_test_window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);
    assert(hwnd != nullptr);

    create_device(hwnd);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    // 기본 메시지 루프입니다:
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    destroy_device();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_PAINT:
        {
            render(hwnd);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return 0;
}