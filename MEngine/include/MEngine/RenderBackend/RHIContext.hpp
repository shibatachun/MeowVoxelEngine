#pragma once

#include "MEngine/RenderBackend/Primitive.hpp"

#include <nvrhi/nvrhi.h>

#include <vector>

namespace MEngine::Camera {
struct CameraState;
}

namespace MEngine::RenderBackend {

class RHIContext {
public:
    virtual ~RHIContext() = default;

    virtual void initialize(void* nativeWindowHandle, const char* applicationName, bool enableRayTracing) = 0;
    virtual void beginFrame() {}
    virtual void endFrame(const Camera::CameraState* camera = nullptr) {}
    virtual void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) {}
    virtual void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives) {}
    [[nodiscard]] virtual bool shootingModeEnabled() const { return false; }
    virtual void shutdown() = 0;

    [[nodiscard]] virtual nvrhi::DeviceHandle device() const = 0;
};

} // namespace MEngine::RenderBackend
