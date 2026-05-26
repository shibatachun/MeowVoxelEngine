#pragma once

#include "D3D12Utils.hpp"

#include <imgui_impl_dx12.h>

#include <chrono>

namespace MEngine::RenderBackend::D3D12 {

class D3D12ImGuiLayer {
public:
    void initialize(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthBufferFormat,
        int framesInFlight,
        uint32_t descriptorCapacity);
    void shutdown();
    void render(
        ID3D12GraphicsCommandList* commandList,
        uint32_t width,
        uint32_t height,
        size_t primitiveCount,
        size_t indexCount);

private:
    static void allocateDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle);
    static void freeDescriptor(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    uint32_t descriptorSize_ = 0;
    uint32_t nextDescriptor_ = 0;
    uint32_t descriptorCapacity_ = 0;
    bool initialized_ = false;
    bool ownsImGuiContext_ = false;
    std::chrono::steady_clock::time_point lastFrameTime_ {};
};

} // namespace MEngine::RenderBackend::D3D12
