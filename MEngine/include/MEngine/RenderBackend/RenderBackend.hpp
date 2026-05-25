#pragma once

#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/Camera/Camera.hpp"
#include "MEngine/MEngine.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/RenderBackend/RHIContext.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace MEngine::RenderBackend {

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(const std::string& applicationName, int width, int height, GraphicsApi graphicsApi, void* nativeWindowHandle, bool enableRayTracing);
    void beginFrame();
    void endFrame(const MEngine::Camera::CameraState& camera);
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    void setDynamicPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    void setMeshAsset(const Resources::MeshAsset& asset);
    void setMeshWorldTransform(const glm::mat4& transform);
    void setMeshSkinningMatrices(const std::vector<glm::mat4>& matrices);
    [[nodiscard]] bool playerControlModeEnabled() const;
    [[nodiscard]] bool shootingModeEnabled() const;
    [[nodiscard]] AnimationSystem::AnimationTuning animationTuning() const;
    bool consumePlayerResetRequested();
    bool consumePlayRequested();
    bool consumeModelLoadRequested(std::string& outPath);
    void setEditorPlayMode(bool enabled);
    void shutdown();

private:
    std::unique_ptr<RHIContext> rhiContext_;
    std::string backendName_;
    bool initialized_ = false;
};

} // namespace MEngine::RenderBackend
