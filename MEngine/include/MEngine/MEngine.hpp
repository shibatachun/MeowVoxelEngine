#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"

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
};

class Engine {
public:
    explicit Engine(EngineConfig config = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void initialize();
    void tick(float deltaSeconds);
    [[nodiscard]] const Camera::CameraState& cameraState() const;
    void setPrimitiveWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives);
    void shutdown();

    [[nodiscard]] bool isRunning() const;
    void requestExit();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine
