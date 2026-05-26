#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"

#include <vector>

namespace SandBox {

bool tryPlaceBlock(
    const MEngine::Camera::CameraState& camera,
    const std::vector<MEngine::RenderBackend::PrimitiveInstance>& visibleBlocks,
    std::vector<MEngine::RenderBackend::PrimitiveInstance>& placedBlocks,
    float blockSize);

} // namespace SandBox
