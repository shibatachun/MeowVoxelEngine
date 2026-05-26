#pragma once

#include "MEngine/Camera/Camera.hpp"

#include <glm/vec3.hpp>

namespace SandBox {

[[nodiscard]] glm::vec3 cameraForward(const MEngine::Camera::CameraState& camera);

} // namespace SandBox
