#include "CameraUtils.hpp"

#include <glm/geometric.hpp>

namespace SandBox {

glm::vec3 cameraForward(const MEngine::Camera::CameraState& camera)
{
    return glm::normalize(
        glm::vec3 { camera.target[0], camera.target[1], camera.target[2] } -
        glm::vec3 { camera.position[0], camera.position[1], camera.position[2] });
}

} // namespace SandBox
