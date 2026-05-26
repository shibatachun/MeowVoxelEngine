#pragma once

namespace MEngine {
class Engine;
}

namespace SandBox {

class PrimitiveWorldStreamer;

void submitVisibleWorld(
    MEngine::Engine& engine,
    const PrimitiveWorldStreamer& worldStreamer,
    bool updateCollision);

} // namespace SandBox
