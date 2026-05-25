#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/InputSystem/InputSystem.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <memory>
#include <string>
#include <vector>

namespace MEngine {

enum class GraphicsApi {
    D3D12,
    Vulkan,
};

struct EngineConfig {
    std::string applicationName = "MeowEngine App";
    int viewportWidth = 1280;
    int viewportHeight = 720;
    GraphicsApi graphicsApi = GraphicsApi::D3D12;
    void* nativeWindowHandle = nullptr;
    bool enableRayTracing = false;
};

class Engine {
public:
    explicit Engine(EngineConfig config = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void initialize();
    void pollInput();
    void tick(float deltaSeconds);
    [[nodiscard]] const Camera::CameraState& cameraState() const;
    [[nodiscard]] bool shootingModeEnabled() const;
    [[nodiscard]] bool playerControlModeEnabled() const;
    bool consumePlayerResetRequested();
    bool consumePlayRequested();
    bool consumeModelLoadRequested(std::string& outPath);
    void setEditorPlayMode(bool enabled);
    [[nodiscard]] AnimationSystem::AnimationTuning animationTuning() const;
    [[nodiscard]] const InputSystem::PlayerInputState& playerInput() const;
    void setCameraState(const Camera::CameraState& state);
    void setCameraExternalControlEnabled(bool enabled);
    void setPlayerInputEnabled(bool enabled);
    void setAnimationState(AnimationSystem::AnimationState state);
    void setPrimitiveWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives);
    void setPrimitiveVisualWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives);
    void setPrimitiveCollisionWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives);
    void setInteractivePrimitives(const std::vector<RenderBackend::PrimitiveInstance>& primitives);
    void setMeshAsset(const Resources::MeshAsset& asset);
    void setMeshWorldTransform(const float transform[16]);
    void shootPhysicsSphere(const float origin[3], const float direction[3]);
    void shutdown();

    [[nodiscard]] bool isRunning() const;
    void requestExit();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine
