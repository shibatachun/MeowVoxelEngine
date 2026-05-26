#include "WorldSubmission.hpp"

#include "PrimitiveWorld.hpp"

#include "MEngine/MEngine.hpp"

namespace SandBox {

void submitVisibleWorld(
    MEngine::Engine& engine,
    const PrimitiveWorldStreamer& worldStreamer,
    bool updateCollision)
{
    engine.setPrimitiveVisualWorld(worldStreamer.visiblePrimitives());
    if (updateCollision) {
        engine.setPrimitiveCollisionWorld(worldStreamer.visiblePrimitives());
    }
}

} // namespace SandBox
