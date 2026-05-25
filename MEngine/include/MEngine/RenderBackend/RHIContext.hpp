#pragma once

#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>
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
    virtual void setMeshAsset(const Resources::MeshAsset& asset) {}
    virtual void setMeshWorldTransform(const glm::mat4& transform) {}
    virtual void setMeshSkinningMatrices(const std::vector<glm::mat4>& matrices) {}
    [[nodiscard]] virtual bool playerControlModeEnabled() const { return false; }
    [[nodiscard]] virtual bool shootingModeEnabled() const { return false; }
    [[nodiscard]] virtual AnimationSystem::AnimationTuning animationTuning() const { return {}; }
    virtual bool consumePlayerResetRequested() { return false; }
    virtual bool consumePlayRequested() { return false; }
    virtual bool consumeModelLoadRequested(std::string& outPath) { (void)outPath; return false; }
    virtual void setEditorPlayMode(bool enabled) { (void)enabled; }
    virtual void shutdown() = 0;

    [[nodiscard]] virtual nvrhi::DeviceHandle device() const = 0;
};

} // namespace MEngine::RenderBackend
