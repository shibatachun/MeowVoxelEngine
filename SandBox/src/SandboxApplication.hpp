#pragma once

#include "CommandLineOptions.hpp"
#include "ModelAssetPreprocessor.hpp"
#include "PlayerController.hpp"
#include "PrimitiveWorld.hpp"

#include "MEngine/RenderBackend/Primitive.hpp"

#include <vector>

namespace MEngine {
class Engine;
}

namespace SandBox {

class SandboxApplication {
public:
    explicit SandboxApplication(const CommandLineOptions& options);

    void initialize(MEngine::Engine& engine);
    void tick(MEngine::Engine& engine, float deltaSeconds);

private:
    [[nodiscard]] PrimitiveWorldConfig makeWorldConfig() const;
    void updateModelRequests(MEngine::Engine& engine);
    void updatePlayMode(MEngine::Engine& engine);
    void updatePlayerMode(MEngine::Engine& engine);
    void updatePlayer(MEngine::Engine& engine, float deltaSeconds);
    void updateWorldStreaming(MEngine::Engine& engine);
    void updateEditorInteraction(MEngine::Engine& engine);

    CommandLineOptions options_;
    ModelAssetPreprocessor modelPreprocessor_;
    PrimitiveWorldConfig worldConfig_ {};
    PrimitiveWorldStreamer worldStreamer_;
    std::vector<MEngine::RenderBackend::PrimitiveInstance> placedBlocks_;
    PlayerControllerState player_;
    bool wasLeftMouseDown_ = false;
    bool wasPlayerMode_ = false;
    bool playMode_ = false;
    bool wasEscapeDown_ = false;
    bool shouldSubmitWorld_ = false;
};

} // namespace SandBox
