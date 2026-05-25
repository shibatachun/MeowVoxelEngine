#include "MEngine/Camera/Camera.hpp"

#include "MEngine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace MEngine::Camera {

namespace {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 add(Vec3 a, Vec3 b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3 scale(Vec3 value, float amount)
{
    return { value.x * amount, value.y * amount, value.z * amount };
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.00001f) {
        return { 0.0f, 0.0f, -1.0f };
    }

    return { value.x / length, value.y / length, value.z / length };
}

Vec3 cameraDirection(float yawRadians, float pitchRadians)
{
    const float cosPitch = std::cos(pitchRadians);
    return normalize({
        cosPitch * std::cos(yawRadians),
        std::sin(pitchRadians),
        cosPitch * std::sin(yawRadians)
    });
}

} // namespace

CameraController::~CameraController()
{
    shutdown();
}

void CameraController::initialize(SDL_Window* window)
{
    if (initialized_) {
        return;
    }

    window_ = window;
    windowId_ = window ? SDL_GetWindowID(window) : 0;
    lastUpdateTime_ = std::chrono::steady_clock::now();
    updateTarget();

    if (window_) {
        eventWatchInstalled_ = SDL_AddEventWatch(&CameraController::sdlEventWatch, this);
    }

    initialized_ = true;
    MENGINE_INFO("[Camera] Initialized");
}

void CameraController::update()
{
    if (!initialized_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - lastUpdateTime_;
    lastUpdateTime_ = now;
    const float deltaSeconds = std::clamp(delta.count(), 0.0f, 0.1f);

    if (externalControlEnabled_) {
        pendingMouseDeltaX_ = 0.0f;
        pendingMouseDeltaY_ = 0.0f;
        if (relativeMouseEnabled_ && window_) {
            SDL_SetWindowRelativeMouseMode(window_, false);
            relativeMouseEnabled_ = false;
        }
        return;
    }

    const bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    if (window_ && relativeMouseEnabled_ == altDown) {
        relativeMouseEnabled_ = !altDown;
        SDL_SetWindowRelativeMouseMode(window_, relativeMouseEnabled_);
        pendingMouseDeltaX_ = 0.0f;
        pendingMouseDeltaY_ = 0.0f;
    }

    if (!altDown) {
        yawRadians_ += pendingMouseDeltaX_ * mouseSensitivity_;
        pitchRadians_ -= pendingMouseDeltaY_ * mouseSensitivity_;
        pitchRadians_ = std::clamp(pitchRadians_, -1.45f, 1.45f);
    }
    pendingMouseDeltaX_ = 0.0f;
    pendingMouseDeltaY_ = 0.0f;

    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    Vec3 movement {};
    const Vec3 forward = cameraDirection(yawRadians_, pitchRadians_);
    const Vec3 right = normalize(cross(forward, { 0.0f, 1.0f, 0.0f }));

    if (!altDown && keyboard) {
        if (keyboard[SDL_SCANCODE_W]) {
            movement = add(movement, forward);
        }
        if (keyboard[SDL_SCANCODE_S]) {
            movement = subtract(movement, forward);
        }
        if (keyboard[SDL_SCANCODE_D]) {
            movement = add(movement, right);
        }
        if (keyboard[SDL_SCANCODE_A]) {
            movement = subtract(movement, right);
        }
    }

    if (dot(movement, movement) > 0.00001f) {
        movement = scale(normalize(movement), moveSpeed_ * deltaSeconds);
        state_.position[0] += movement.x;
        state_.position[1] += movement.y;
        state_.position[2] += movement.z;
    }

    updateTarget();
}

void CameraController::shutdown()
{
    if (!initialized_) {
        return;
    }

    if (eventWatchInstalled_) {
        SDL_RemoveEventWatch(&CameraController::sdlEventWatch, this);
        eventWatchInstalled_ = false;
    }

    if (relativeMouseEnabled_ && window_) {
        SDL_SetWindowRelativeMouseMode(window_, false);
        relativeMouseEnabled_ = false;
    }

    window_ = nullptr;
    windowId_ = 0;
    pendingMouseDeltaX_ = 0.0f;
    pendingMouseDeltaY_ = 0.0f;
    initialized_ = false;
    MENGINE_INFO("[Camera] Shutdown");
}

const CameraState& CameraController::state() const
{
    return state_;
}

void CameraController::setState(const CameraState& state)
{
    state_ = state;
}

void CameraController::setExternalControlEnabled(bool enabled)
{
    externalControlEnabled_ = enabled;
    if (enabled) {
        pendingMouseDeltaX_ = 0.0f;
        pendingMouseDeltaY_ = 0.0f;
        if (relativeMouseEnabled_ && window_) {
            SDL_SetWindowRelativeMouseMode(window_, false);
            relativeMouseEnabled_ = false;
        }
    }
}

float& CameraController::moveSpeed()
{
    return moveSpeed_;
}

float& CameraController::mouseSensitivity()
{
    return mouseSensitivity_;
}

void CameraController::handleSdlEvent(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_MOTION) {
        return;
    }

    if (windowId_ != 0 && event.motion.windowID != 0 && event.motion.windowID != windowId_) {
        return;
    }

    if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
        return;
    }

    pendingMouseDeltaX_ += event.motion.xrel;
    pendingMouseDeltaY_ += event.motion.yrel;
}

void CameraController::updateTarget()
{
    const Vec3 direction = cameraDirection(yawRadians_, pitchRadians_);
    state_.target[0] = state_.position[0] + direction.x;
    state_.target[1] = state_.position[1] + direction.y;
    state_.target[2] = state_.position[2] + direction.z;
}

bool CameraController::sdlEventWatch(void* userdata, SDL_Event* event)
{
    if (userdata && event) {
        static_cast<CameraController*>(userdata)->handleSdlEvent(*event);
    }

    return true;
}

} // namespace MEngine::Camera
