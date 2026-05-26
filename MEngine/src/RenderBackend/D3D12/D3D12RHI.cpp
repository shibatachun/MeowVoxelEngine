#include "MEngine/RenderBackend/D3D12/D3D12RHI.hpp"

#include "MEngine/Core/Log.hpp"

#include "D3D12ImGuiLayer.hpp"
#include "D3D12PrimitiveRenderer.hpp"
#include "D3D12Utils.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <Windows.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <nvrhi/d3d12.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef MENGINE_SHADER_BINARY_DIR
#define MENGINE_SHADER_BINARY_DIR "."
#endif

namespace MEngine::RenderBackend::D3D12 {

namespace {

constexpr uint32_t FramesInFlight = 2;
constexpr DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthBufferFormat = DXGI_FORMAT_D32_FLOAT;

class NvrhiMessageCallback final : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* messageText) override
    {
        switch (severity) {
        case nvrhi::MessageSeverity::Info:
            MENGINE_INFO("[NVRHI D3D12] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Warning:
            MENGINE_WARN("[NVRHI D3D12] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Error:
            MENGINE_ERROR("[NVRHI D3D12] {}", messageText);
            break;
        case nvrhi::MessageSeverity::Fatal:
            MENGINE_ERROR("[NVRHI D3D12 Fatal] {}", messageText);
            break;
        }
    }
};

NvrhiMessageCallback g_NvrhiMessageCallback;

} // namespace

class D3D12RHI::Impl {
public:
    ~Impl()
    {
        shutdown();
    }

    void initialize(SDL_Window* window, const char* applicationName, bool enableRayTracing)
    {
        (void)applicationName;
        if (!window) {
            throw std::runtime_error("D3D12 requires a valid SDL window handle");
        }

#if !defined(_WIN32)
        (void)enableRayTracing;
        throw std::runtime_error("D3D12 backend is only available on Windows");
#else
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr));
        if (!hwnd) {
            throw std::runtime_error("SDL window does not expose a Win32 HWND for D3D12");
        }

        window_ = window;
        hwnd_ = hwnd;
        rayTracingRequested_ = enableRayTracing;

        createFactory();
        createDevice();
        createCommandObjects();
        createSwapchain();
        createRenderTargets();
        createDepthBuffer();
        createNvrhiDevice();

        const std::filesystem::path shaderDir = std::filesystem::u8path(MENGINE_SHADER_BINARY_DIR) / "D3D12";
        primitiveRenderer_.initialize(device_.Get(), BackBufferFormat, DepthBufferFormat, shaderDir);
        imguiLayer_.initialize(device_.Get(), commandQueue_.Get(), BackBufferFormat, DepthBufferFormat, FramesInFlight, 64);

        MENGINE_INFO("[RenderBackend] D3D12 RHI initialized with {}", adapterName_);
        if (rayTracingRequested_) {
            MENGINE_WARN("[RenderBackend] D3D12 ray tracing path is not wired yet; continuing without DXR passes");
        }
#endif
    }

    void beginFrame()
    {
    }

    void endFrame(const Camera::CameraState* camera)
    {
        if (!device_ || !swapchain_) {
            return;
        }

        FrameContext& frame = frames_[frameIndex_];
        waitForFrame(frame);

        checkHResult(frame.commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
        checkHResult(commandList_->Reset(frame.commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");

        ID3D12Resource* renderTarget = renderTargets_[frameIndex_].Get();
        const D3D12_RESOURCE_BARRIER toRenderTarget =
            transitionBarrier(renderTarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList_->ResourceBarrier(1, &toRenderTarget);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHandle(frameIndex_);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        const float clearColor[] = { 0.04f, 0.055f, 0.075f, 1.0f };
        commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        commandList_->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        primitiveRenderer_.draw(commandList_.Get(), camera, swapchainWidth_, swapchainHeight_);
        imguiLayer_.render(
            commandList_.Get(),
            swapchainWidth_,
            swapchainHeight_,
            primitiveRenderer_.primitiveCount(),
            primitiveRenderer_.indexCount());

        const D3D12_RESOURCE_BARRIER toPresent =
            transitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList_->ResourceBarrier(1, &toPresent);

        checkHResult(commandList_->Close(), "ID3D12GraphicsCommandList::Close");
        ID3D12CommandList* commandLists[] = { commandList_.Get() };
        commandQueue_->ExecuteCommandLists(1, commandLists);

        checkHResult(swapchain_->Present(1, 0), "IDXGISwapChain::Present");
        signalFrame(frame);
        frameIndex_ = swapchain_->GetCurrentBackBufferIndex();

        ++garbageCollectionFrameCounter_;
        if (garbageCollectionFrameCounter_ >= 60 && nvrhiDevice_) {
            nvrhiDevice_->runGarbageCollection();
            garbageCollectionFrameCounter_ = 0;
        }
    }

    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
    {
        waitForGpuIdle();
        primitiveRenderer_.setStaticPrimitives(primitives);
    }

    void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
    {
        primitiveRenderer_.setDynamicPrimitives(primitives);
    }

    void shutdown()
    {
        if (commandQueue_ && fence_) {
            waitForGpuIdle();
        }

        imguiLayer_.shutdown();
        primitiveRenderer_.shutdown();
        nvrhiDevice_ = nullptr;
        depthBuffer_ = nullptr;
        dsvHeap_ = nullptr;
        renderTargets_.fill(nullptr);
        swapchain_ = nullptr;
        commandList_ = nullptr;
        for (FrameContext& frame : frames_) {
            frame.commandAllocator = nullptr;
            frame.fenceValue = 0;
        }
        rtvHeap_ = nullptr;
        if (fenceEvent_) {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        fence_ = nullptr;
        commandQueue_ = nullptr;
        device_ = nullptr;
        adapter_ = nullptr;
        factory_ = nullptr;
        window_ = nullptr;
        hwnd_ = nullptr;
    }

    [[nodiscard]] nvrhi::DeviceHandle nvrhiDevice() const
    {
        return nvrhiDevice_;
    }

private:
    struct FrameContext {
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        uint64_t fenceValue = 0;
    };

    void createFactory()
    {
        UINT flags = 0;
#if defined(_DEBUG) && defined(MENGINE_D3D12_ENABLE_DEBUG_LAYER)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        checkHResult(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    }

    void createDevice()
    {
        ComPtr<IDXGIAdapter1> candidate;
        for (UINT index = 0; factory_->EnumAdapterByGpuPreference(
                 index,
                 DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&candidate)) != DXGI_ERROR_NOT_FOUND;
             ++index) {
            DXGI_ADAPTER_DESC1 desc {};
            candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)))) {
                adapter_ = candidate;
                adapterName_ = narrow(desc.Description);
                break;
            }
        }

        if (!device_) {
            checkHResult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");
            adapterName_ = "Default Adapter";
        }
    }

    void createCommandObjects()
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        checkHResult(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)), "ID3D12Device::CreateCommandQueue");

        for (FrameContext& frame : frames_) {
            checkHResult(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.commandAllocator)),
                "ID3D12Device::CreateCommandAllocator");
        }

        checkHResult(device_->CreateCommandList(
                         0,
                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                         frames_[0].commandAllocator.Get(),
                         nullptr,
                         IID_PPV_ARGS(&commandList_)),
            "ID3D12Device::CreateCommandList");
        checkHResult(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

        checkHResult(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "ID3D12Device::CreateFence");
        fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent_) {
            throw std::runtime_error("CreateEvent failed for D3D12 fence");
        }
    }

    void createSwapchain()
    {
        int width = 1;
        int height = 1;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        swapchainWidth_ = static_cast<uint32_t>((std::max)(width, 1));
        swapchainHeight_ = static_cast<uint32_t>((std::max)(height, 1));

        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = swapchainWidth_;
        desc.Height = swapchainHeight_;
        desc.Format = BackBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = FramesInFlight;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> swapchain1;
        checkHResult(factory_->CreateSwapChainForHwnd(
                         commandQueue_.Get(),
                         hwnd_,
                         &desc,
                         nullptr,
                         nullptr,
                         &swapchain1),
            "IDXGIFactory::CreateSwapChainForHwnd");
        checkHResult(factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory::MakeWindowAssociation");
        checkHResult(swapchain1.As(&swapchain_), "IDXGISwapChain1::QueryInterface");
        frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
    }

    void createRenderTargets()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = FramesInFlight;
        checkHResult(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)), "ID3D12Device::CreateDescriptorHeap RTV");
        rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (uint32_t i = 0; i < FramesInFlight; ++i) {
            checkHResult(swapchain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i])), "IDXGISwapChain::GetBuffer");
            device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, rtvHandle(i));
        }
    }

    void createDepthBuffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDesc.NumDescriptors = 1;
        checkHResult(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap_)), "ID3D12Device::CreateDescriptorHeap DSV");

        D3D12_HEAP_PROPERTIES heapProperties {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = swapchainWidth_;
        desc.Height = swapchainHeight_;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DepthBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue {};
        clearValue.Format = DepthBufferFormat;
        clearValue.DepthStencil.Depth = 1.0f;

        checkHResult(device_->CreateCommittedResource(
                         &heapProperties,
                         D3D12_HEAP_FLAG_NONE,
                         &desc,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE,
                         &clearValue,
                         IID_PPV_ARGS(&depthBuffer_)),
            "ID3D12Device::CreateCommittedResource depth buffer");
        device_->CreateDepthStencilView(depthBuffer_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    }

    void createNvrhiDevice()
    {
        nvrhi::d3d12::DeviceDesc desc {};
        desc.errorCB = &g_NvrhiMessageCallback;
        desc.pDevice = device_.Get();
        desc.pGraphicsCommandQueue = commandQueue_.Get();
        desc.pComputeCommandQueue = commandQueue_.Get();
        desc.pCopyCommandQueue = commandQueue_.Get();
        nvrhiDevice_ = nvrhi::d3d12::createDevice(desc);
        if (!nvrhiDevice_) {
            throw std::runtime_error("nvrhi::d3d12::createDevice returned null");
        }
    }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * rtvDescriptorSize_;
        return handle;
    }

    void signalFrame(FrameContext& frame)
    {
        const uint64_t value = ++fenceValue_;
        checkHResult(commandQueue_->Signal(fence_.Get(), value), "ID3D12CommandQueue::Signal");
        frame.fenceValue = value;
    }

    void waitForFrame(FrameContext& frame)
    {
        if (!fence_ || frame.fenceValue == 0 || fence_->GetCompletedValue() >= frame.fenceValue) {
            return;
        }

        checkHResult(fence_->SetEventOnCompletion(frame.fenceValue, fenceEvent_), "ID3D12Fence::SetEventOnCompletion");
        WaitForSingleObject(fenceEvent_, INFINITE);
        frame.fenceValue = 0;
    }

    void waitForGpuIdle()
    {
        if (!commandQueue_ || !fence_) {
            return;
        }

        const uint64_t value = ++fenceValue_;
        checkHResult(commandQueue_->Signal(fence_.Get(), value), "ID3D12CommandQueue::Signal idle");
        if (fence_->GetCompletedValue() < value) {
            checkHResult(fence_->SetEventOnCompletion(value, fenceEvent_), "ID3D12Fence::SetEventOnCompletion idle");
            WaitForSingleObject(fenceEvent_, INFINITE);
        }

        for (FrameContext& frame : frames_) {
            frame.fenceValue = 0;
        }
    }

    static std::string narrow(const wchar_t* text)
    {
        std::string result;
        while (text && *text) {
            const wchar_t ch = *text++;
            result.push_back(ch >= 0 && ch <= 127 ? static_cast<char>(ch) : '?');
        }
        return result;
    }

    SDL_Window* window_ = nullptr;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    ComPtr<ID3D12Resource> depthBuffer_;
    std::array<ComPtr<ID3D12Resource>, FramesInFlight> renderTargets_;
    std::array<FrameContext, FramesInFlight> frames_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    nvrhi::DeviceHandle nvrhiDevice_;
    D3D12PrimitiveRenderer primitiveRenderer_;
    D3D12ImGuiLayer imguiLayer_;
    std::string adapterName_;
    uint32_t swapchainWidth_ = 1;
    uint32_t swapchainHeight_ = 1;
    uint32_t frameIndex_ = 0;
    uint32_t rtvDescriptorSize_ = 0;
    uint32_t garbageCollectionFrameCounter_ = 0;
    uint64_t fenceValue_ = 0;
    bool rayTracingRequested_ = false;
};

D3D12RHI::D3D12RHI()
    : impl_(std::make_unique<Impl>()) {}

D3D12RHI::~D3D12RHI() = default;

void D3D12RHI::initialize(void* nativeWindowHandle, const char* applicationName, bool enableRayTracing)
{
    impl_->initialize(static_cast<SDL_Window*>(nativeWindowHandle), applicationName, enableRayTracing);
}

void D3D12RHI::beginFrame()
{
    impl_->beginFrame();
}

void D3D12RHI::endFrame(const Camera::CameraState* camera)
{
    impl_->endFrame(camera);
}

void D3D12RHI::setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    impl_->setPrimitiveInstances(primitives);
}

void D3D12RHI::setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    impl_->setDynamicPrimitiveInstances(primitives);
}

void D3D12RHI::shutdown()
{
    impl_->shutdown();
}

nvrhi::DeviceHandle D3D12RHI::device() const
{
    return impl_->nvrhiDevice();
}

} // namespace MEngine::RenderBackend::D3D12
