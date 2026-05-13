#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/MEngine.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"
#include "MEngine/RenderBackend/RHIContext.hpp"

#include <memory>
#include <string>

namespace MEngine::RenderBackend {

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(const std::string& applicationName, int width, int height, GraphicsApi graphicsApi, void* nativeWindowHandle);
    void beginFrame();
    void endFrame(const MEngine::Camera::CameraState& camera);
    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives);
    void shutdown();

private:
    std::unique_ptr<RHIContext> rhiContext_;
    std::string backendName_;
    bool initialized_ = false;
};

} // namespace MEngine::RenderBackend
