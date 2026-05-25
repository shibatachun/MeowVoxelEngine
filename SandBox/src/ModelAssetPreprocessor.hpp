#pragma once

namespace MEngine {
class Engine;
}

namespace SandBox {

class ModelAssetPreprocessor {
public:
    void loadDefaultModel(MEngine::Engine& engine) const;
    bool loadModel(MEngine::Engine& engine, const char* modelPath) const;
};

} // namespace SandBox
