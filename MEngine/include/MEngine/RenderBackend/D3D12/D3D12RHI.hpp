#pragma once

#include "MEngine/RenderBackend/RHIContext.hpp"

#include <memory>

namespace MEngine::RenderBackend::D3D12 {

class D3D12RHI final : public RHIContext {
public:
    D3D12RHI();
    ~D3D12RHI() override;

    D3D12RHI(const D3D12RHI&) = delete;
    D3D12RHI& operator=(const D3D12RHI&) = delete;

    void initialize(void* nativeWindowHandle, const char* applicationName, bool enableRayTracing) override;
    void beginFrame() override;
    void endFrame(const Camera::CameraState* camera = nullptr) override;
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) override;
    void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) override;
    void shutdown() override;

    [[nodiscard]] nvrhi::DeviceHandle device() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine::RenderBackend::D3D12
