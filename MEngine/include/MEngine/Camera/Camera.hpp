#pragma once

#include <chrono>

union SDL_Event;
struct SDL_Window;

namespace MEngine::Camera {

struct CameraState {
    float position[3] { 0.0f, 1.2f, 4.2f };
    float target[3] { 0.0f, 1.0f, 3.2f };
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

class CameraController {
public:
    CameraController() = default;
    ~CameraController();

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    void initialize(SDL_Window* window);
    void update();
    void setState(const CameraState& state);
    void setExternalControlEnabled(bool enabled);
    void shutdown();

    [[nodiscard]] const CameraState& state() const;

    float& moveSpeed();
    float& mouseSensitivity();

private:
    void handleSdlEvent(const SDL_Event& event);
    void updateTarget();

    static bool sdlEventWatch(void* userdata, SDL_Event* event);

    CameraState state_;
    SDL_Window* window_ = nullptr;
    unsigned int windowId_ = 0;
    bool initialized_ = false;
    bool eventWatchInstalled_ = false;
    bool externalControlEnabled_ = false;
    bool relativeMouseEnabled_ = false;
    float yawRadians_ = -1.57079632679f;
    float pitchRadians_ = -0.2f;
    float moveSpeed_ = 3.5f;
    float mouseSensitivity_ = 0.0025f;
    float pendingMouseDeltaX_ = 0.0f;
    float pendingMouseDeltaY_ = 0.0f;
    std::chrono::steady_clock::time_point lastUpdateTime_ {};
};

} // namespace MEngine::Camera
