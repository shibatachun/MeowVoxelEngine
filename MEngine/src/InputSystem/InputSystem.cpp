#include "MEngine/InputSystem/InputSystem.hpp"

#include "MEngine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <string>

namespace MEngine::InputSystem {

void Input::initialize(SDL_Window* window, const std::string& configPath)
{
    window_ = window;
    configPath_ = configPath;
    loadOrCreateConfig();
    initialized_ = true;
    MENGINE_INFO("[InputSystem] Initialized");
}

void Input::poll()
{
    if (!initialized_) {
        return;
    }

    playerInput_ = {};
    playerInput_.modifierDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    if (!playerInputEnabled_) {
        return;
    }

    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, !playerInput_.modifierDown);
    }

    const bool* keyboard = SDL_GetKeyboardState(nullptr);
    if (keyboard) {
        if (keyboard[moveForwardKey_]) {
            playerInput_.moveForward += 1.0f;
        }
        if (keyboard[moveBackwardKey_]) {
            playerInput_.moveForward -= 1.0f;
        }
        if (keyboard[moveRightKey_]) {
            playerInput_.moveRight += 1.0f;
        }
        if (keyboard[moveLeftKey_]) {
            playerInput_.moveRight -= 1.0f;
        }
        playerInput_.jumpPressed = keyboard[jumpKey_];
    }

    if (!playerInput_.modifierDown) {
        SDL_GetRelativeMouseState(&playerInput_.mouseDeltaX, &playerInput_.mouseDeltaY);
    }
}

void Input::setPlayerInputEnabled(bool enabled)
{
    if (playerInputEnabled_ == enabled) {
        return;
    }

    playerInputEnabled_ = enabled;
    playerInput_ = {};
    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, enabled);
    }
    float ignoredX = 0.0f;
    float ignoredY = 0.0f;
    SDL_GetRelativeMouseState(&ignoredX, &ignoredY);
}

void Input::shutdown()
{
    if (initialized_) {
        saveConfig();
        if (window_) {
            SDL_SetWindowRelativeMouseMode(window_, false);
        }
        MENGINE_INFO("[InputSystem] Shutdown");
        initialized_ = false;
    }
}

void Input::loadOrCreateConfig()
{
    std::ifstream stream(configPath_);
    if (!stream) {
        saveConfig();
        return;
    }

    std::string line;
    while (std::getline(stream, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "MoveForward") {
            moveForwardKey_ = parseScancode(value, moveForwardKey_);
        } else if (key == "MoveBackward") {
            moveBackwardKey_ = parseScancode(value, moveBackwardKey_);
        } else if (key == "MoveLeft") {
            moveLeftKey_ = parseScancode(value, moveLeftKey_);
        } else if (key == "MoveRight") {
            moveRightKey_ = parseScancode(value, moveRightKey_);
        } else if (key == "Jump") {
            jumpKey_ = parseScancode(value, jumpKey_);
        }
    }
}

void Input::saveConfig() const
{
    std::ofstream stream(configPath_, std::ios::trunc);
    if (!stream) {
        return;
    }

    stream << "[Player]\n";
    stream << "MoveForward=" << scancodeName(moveForwardKey_) << "\n";
    stream << "MoveBackward=" << scancodeName(moveBackwardKey_) << "\n";
    stream << "MoveLeft=" << scancodeName(moveLeftKey_) << "\n";
    stream << "MoveRight=" << scancodeName(moveRightKey_) << "\n";
    stream << "Jump=" << scancodeName(jumpKey_) << "\n";
}

SDL_Scancode Input::bindingForAction(const std::string&, SDL_Scancode fallback) const
{
    return fallback;
}

std::string Input::scancodeName(SDL_Scancode scancode) const
{
    const char* name = SDL_GetScancodeName(scancode);
    return name && name[0] != '\0' ? name : "Unknown";
}

SDL_Scancode Input::parseScancode(const std::string& value, SDL_Scancode fallback) const
{
    const SDL_Scancode scancode = SDL_GetScancodeFromName(value.c_str());
    return scancode == SDL_SCANCODE_UNKNOWN ? fallback : scancode;
}

} // namespace MEngine::InputSystem
