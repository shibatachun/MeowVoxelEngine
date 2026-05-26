#include "D3D12ImGuiLayer.hpp"

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <algorithm>
#include <stdexcept>

namespace MEngine::RenderBackend::D3D12 {

void D3D12ImGuiLayer::initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    DXGI_FORMAT backBufferFormat,
    DXGI_FORMAT depthBufferFormat,
    int framesInFlight,
    uint32_t descriptorCapacity)
{
    descriptorCapacity_ = descriptorCapacity;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = descriptorCapacity_;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    checkHResult(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvHeap_)),
        "ID3D12Device::CreateDescriptorHeap ImGui SRV");
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        ownsImGuiContext_ = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::StyleColorsDark();

    ImGui_ImplDX12_InitInfo initInfo {};
    initInfo.Device = device;
    initInfo.CommandQueue = commandQueue;
    initInfo.NumFramesInFlight = framesInFlight;
    initInfo.RTVFormat = backBufferFormat;
    initInfo.DSVFormat = depthBufferFormat;
    initInfo.SrvDescriptorHeap = srvHeap_.Get();
    initInfo.SrvDescriptorAllocFn = &D3D12ImGuiLayer::allocateDescriptor;
    initInfo.SrvDescriptorFreeFn = &D3D12ImGuiLayer::freeDescriptor;
    initInfo.UserData = this;
    if (!ImGui_ImplDX12_Init(&initInfo)) {
        throw std::runtime_error("ImGui_ImplDX12_Init failed");
    }

    lastFrameTime_ = std::chrono::steady_clock::now();
    initialized_ = true;
}

void D3D12ImGuiLayer::shutdown()
{
    if (initialized_) {
        ImGui_ImplDX12_Shutdown();
        initialized_ = false;
    }
    if (ownsImGuiContext_ && ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
        ownsImGuiContext_ = false;
    }
    srvHeap_ = nullptr;
}

void D3D12ImGuiLayer::render(
    ID3D12GraphicsCommandList* commandList,
    uint32_t width,
    uint32_t height,
    size_t primitiveCount,
    size_t indexCount)
{
    if (!initialized_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - lastFrameTime_;
    lastFrameTime_ = now;

    ImGui_ImplDX12_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = (std::max)(delta.count(), 1.0f / 10000000.0f);

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("MeowEngine D3D12 Stats", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);
    ImGui::Text("D3D12");
    ImGui::Text("FPS %.1f", io.Framerate);
    ImGui::Text("Frame %.3f ms", 1000.0f / (std::max)(io.Framerate, 0.001f));
    ImGui::Text("Primitives %zu", primitiveCount);
    ImGui::Text("Indices %zu", indexCount);
    ImGui::End();
    ImGui::Render();

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void D3D12ImGuiLayer::allocateDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* self = static_cast<D3D12ImGuiLayer*>(info->UserData);
    const uint32_t index = self->nextDescriptor_++;
    if (index >= self->descriptorCapacity_) {
        throw std::runtime_error("D3D12 ImGui SRV descriptor heap exhausted");
    }

    *outCpuHandle = self->srvHeap_->GetCPUDescriptorHandleForHeapStart();
    outCpuHandle->ptr += static_cast<SIZE_T>(index) * self->descriptorSize_;
    *outGpuHandle = self->srvHeap_->GetGPUDescriptorHandleForHeapStart();
    outGpuHandle->ptr += static_cast<UINT64>(index) * self->descriptorSize_;
}

void D3D12ImGuiLayer::freeDescriptor(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
    (void)info;
    (void)cpuHandle;
    (void)gpuHandle;
}

} // namespace MEngine::RenderBackend::D3D12
