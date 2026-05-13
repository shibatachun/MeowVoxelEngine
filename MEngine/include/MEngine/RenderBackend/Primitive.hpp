#pragma once

namespace MEngine::RenderBackend {

enum class PrimitiveType {
    Triangle,
    Quad,
    Cube,
    Sphere
};

enum PrimitiveFaceBits {
    PrimitiveFaceNegativeZ = 1 << 0,
    PrimitiveFacePositiveZ = 1 << 1,
    PrimitiveFaceNegativeY = 1 << 2,
    PrimitiveFacePositiveY = 1 << 3,
    PrimitiveFacePositiveX = 1 << 4,
    PrimitiveFaceNegativeX = 1 << 5,
    PrimitiveFaceAll = PrimitiveFaceNegativeZ | PrimitiveFacePositiveZ | PrimitiveFaceNegativeY |
        PrimitiveFacePositiveY | PrimitiveFacePositiveX | PrimitiveFaceNegativeX
};

struct PrimitiveInstance {
    PrimitiveType type = PrimitiveType::Cube;
    float position[3] { 0.0f, 0.0f, 0.0f };
    float size = 1.0f;
    float color[3] { 1.0f, 1.0f, 1.0f };
    unsigned int visibleFaces = PrimitiveFaceAll;
};

} // namespace MEngine::RenderBackend
