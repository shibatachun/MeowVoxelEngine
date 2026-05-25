#pragma once

#include <SDL3/SDL_scancode.h>

#include <string>

struct SDL_Window;

namespace MEngine::InputSystem {

struct PlayerInputState {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    bool jumpPressed = false;
    bool modifierDown = false;
};

class Input {
public:
    void initialize(SDL_Window* window, const std::string& configPath = "MEngineInput.ini");
    void poll();
    void setPlayerInputEnabled(bool enabled);
    [[nodiscard]] const PlayerInputState& playerInput() const { return playerInput_; }
    void shutdown();

private:
    void loadOrCreateConfig();
    void saveConfig() const;
    [[nodiscard]] SDL_Scancode bindingForAction(const std::string& actionName, SDL_Scancode fallback) const;
    [[nodiscard]] std::string scancodeName(SDL_Scancode scancode) const;
    [[nodiscard]] SDL_Scancode parseScancode(const std::string& value, SDL_Scancode fallback) const;

    bool initialized_ = false;
    bool playerInputEnabled_ = false;
    SDL_Window* window_ = nullptr;
    std::string configPath_;
    SDL_Scancode moveForwardKey_ = SDL_SCANCODE_W;
    SDL_Scancode moveBackwardKey_ = SDL_SCANCODE_S;
    SDL_Scancode moveLeftKey_ = SDL_SCANCODE_A;
    SDL_Scancode moveRightKey_ = SDL_SCANCODE_D;
    SDL_Scancode jumpKey_ = SDL_SCANCODE_SPACE;
    PlayerInputState playerInput_;
};

} // namespace MEngine::InputSystem
