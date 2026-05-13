#include "MEngine/MEngine.hpp"

#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/Audio/AudioSystem.hpp"
#include "MEngine/Camera/Camera.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/InputSystem/InputSystem.hpp"
#include "MEngine/Physics/PhysicsWorld.hpp"
#include "MEngine/RenderBackend/RenderBackend.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace MEngine {

class Engine::Impl {
public:
    explicit Impl(EngineConfig engineConfig)
        : config(std::move(engineConfig)) {}

    EngineConfig config;
    RenderBackend::Renderer renderer;
    Camera::CameraController camera;
    Physics::PhysicsWorld physicsWorld;
    Audio::AudioSystem audioSystem;
    InputSystem::Input input;
    AnimationSystem::Animator animator;
    bool running = false;
    bool initialized = false;
};

Engine::Engine(EngineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Engine::~Engine()
{
    shutdown();
}

void Engine::initialize()
{
    if (impl_->initialized) {
        return;
    }

    Core::initializeLogging();
    MENGINE_INFO("[MEngine] Initializing {}", impl_->config.applicationName);
    impl_->renderer.initialize(
        impl_->config.applicationName,
        impl_->config.viewportWidth,
        impl_->config.viewportHeight,
        impl_->config.graphicsApi,
        impl_->config.nativeWindowHandle);
    impl_->camera.initialize(static_cast<SDL_Window*>(impl_->config.nativeWindowHandle));
    impl_->physicsWorld.initialize();
    impl_->audioSystem.initialize();
    impl_->input.initialize();
    impl_->animator.initialize();

    impl_->running = true;
    impl_->initialized = true;
}

void Engine::tick(float deltaSeconds)
{
    if (!impl_->running) {
        return;
    }

    impl_->input.poll();
    impl_->camera.update();
    impl_->physicsWorld.step(deltaSeconds);
    impl_->animator.update(deltaSeconds);
    impl_->audioSystem.update();

    impl_->renderer.beginFrame();
    impl_->renderer.endFrame(impl_->camera.state());
}

const Camera::CameraState& Engine::cameraState() const
{
    return impl_->camera.state();
}

void Engine::setPrimitiveWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives)
{
    impl_->renderer.setPrimitiveInstances(primitives);
}

void Engine::shutdown()
{
    if (!impl_->initialized) {
        return;
    }

    impl_->animator.shutdown();
    impl_->input.shutdown();
    impl_->camera.shutdown();
    impl_->audioSystem.shutdown();
    impl_->physicsWorld.shutdown();
    impl_->renderer.shutdown();

    impl_->running = false;
    impl_->initialized = false;
    MENGINE_INFO("[MEngine] Shutdown complete");
}

bool Engine::isRunning() const
{
    return impl_->running;
}

void Engine::requestExit()
{
    impl_->running = false;
}

} // namespace MEngine
