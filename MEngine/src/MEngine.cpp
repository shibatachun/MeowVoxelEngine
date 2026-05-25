#include "MEngine/MEngine.hpp"

#include "MEngine/AnimationSystem/AnimationSystem.hpp"
#include "MEngine/Audio/AudioSystem.hpp"
#include "MEngine/Camera/Camera.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/InputSystem/InputSystem.hpp"
#include "MEngine/Physics/PhysicsWorld.hpp"
#include "MEngine/RenderBackend/RenderBackend.hpp"

#include <SDL3/SDL.h>
#include <glm/vec3.hpp>

#include <cstring>
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
        impl_->config.nativeWindowHandle,
        impl_->config.enableRayTracing);
    impl_->camera.initialize(static_cast<SDL_Window*>(impl_->config.nativeWindowHandle));
    impl_->physicsWorld.initialize();
    impl_->audioSystem.initialize();
    impl_->input.initialize(static_cast<SDL_Window*>(impl_->config.nativeWindowHandle));
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
    impl_->animator.setTuning(impl_->renderer.animationTuning());
    impl_->animator.update(deltaSeconds);
    impl_->audioSystem.update();

    impl_->renderer.beginFrame();
    impl_->renderer.setMeshSkinningMatrices(impl_->animator.skinningMatrices());
    impl_->renderer.setDynamicPrimitiveInstances(impl_->physicsWorld.dynamicPrimitives());
    impl_->renderer.endFrame(impl_->camera.state());
}

void Engine::pollInput()
{
    impl_->input.poll();
}

const Camera::CameraState& Engine::cameraState() const
{
    return impl_->camera.state();
}

bool Engine::shootingModeEnabled() const
{
    return impl_->renderer.shootingModeEnabled();
}

bool Engine::playerControlModeEnabled() const
{
    return impl_->renderer.playerControlModeEnabled();
}

bool Engine::consumePlayerResetRequested()
{
    return impl_->renderer.consumePlayerResetRequested();
}

bool Engine::consumePlayRequested()
{
    return impl_->renderer.consumePlayRequested();
}

bool Engine::consumeModelLoadRequested(std::string& outPath)
{
    return impl_->renderer.consumeModelLoadRequested(outPath);
}

void Engine::setEditorPlayMode(bool enabled)
{
    impl_->renderer.setEditorPlayMode(enabled);
}

AnimationSystem::AnimationTuning Engine::animationTuning() const
{
    return impl_->renderer.animationTuning();
}

const InputSystem::PlayerInputState& Engine::playerInput() const
{
    return impl_->input.playerInput();
}

void Engine::setCameraState(const Camera::CameraState& state)
{
    impl_->camera.setState(state);
}

void Engine::setPlayerInputEnabled(bool enabled)
{
    impl_->input.setPlayerInputEnabled(enabled);
}

void Engine::setAnimationState(AnimationSystem::AnimationState state)
{
    impl_->animator.setAnimationState(state);
}

void Engine::setCameraExternalControlEnabled(bool enabled)
{
    impl_->camera.setExternalControlEnabled(enabled);
}

void Engine::setPrimitiveWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives)
{
    setPrimitiveCollisionWorld(primitives);
    setPrimitiveVisualWorld(primitives);
}

void Engine::setPrimitiveVisualWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives)
{
    impl_->renderer.setPrimitiveInstances(primitives);
}

void Engine::setPrimitiveCollisionWorld(const std::vector<RenderBackend::PrimitiveInstance>& primitives)
{
    impl_->physicsWorld.setTerrainColliders(primitives);
}

void Engine::setInteractivePrimitives(const std::vector<RenderBackend::PrimitiveInstance>& primitives)
{
    impl_->physicsWorld.setInteractiveColliders(primitives);
}

void Engine::setMeshAsset(const Resources::MeshAsset& asset)
{
    impl_->animator.setMeshAsset(asset);
    impl_->renderer.setMeshAsset(asset);
}

void Engine::setMeshWorldTransform(const float transform[16])
{
    glm::mat4 matrix(1.0f);
    std::memcpy(&matrix[0][0], transform, sizeof(float) * 16);
    impl_->renderer.setMeshWorldTransform(matrix);
}

void Engine::shootPhysicsSphere(const float origin[3], const float direction[3])
{
    impl_->physicsWorld.shootSphere(
        glm::vec3(origin[0], origin[1], origin[2]),
        glm::vec3(direction[0], direction[1], direction[2]));
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
