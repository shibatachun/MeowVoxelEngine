#pragma once

#include "MEngine/RenderBackend/Primitive.hpp"

#include <cstdint>
#include <vector>

namespace SandBox {

struct PrimitiveWorldConfig {
    uint32_t seed = 0;
    int viewDistanceChunks = 3;
    int worldSizeChunks = 4096;
    int chunkSize = 16;
    float cellSize = 0.95f;
    float noiseFrequency = 0.085f;
    float heightScale = 7.0f;
};

struct ChunkCoord {
    int x = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const ChunkCoord& other) const
    {
        return x == other.x && z == other.z;
    }
};

class PrimitiveWorldGenerator {
public:
    explicit PrimitiveWorldGenerator(uint32_t seed);

    [[nodiscard]] std::vector<MEngine::RenderBackend::PrimitiveInstance> generate(const PrimitiveWorldConfig& config) const;
    [[nodiscard]] std::vector<MEngine::RenderBackend::PrimitiveInstance> generateChunk(const PrimitiveWorldConfig& config, ChunkCoord coord) const;
    [[nodiscard]] float fractalNoise(float x, float z) const;

private:
    [[nodiscard]] float noise(float x, float z) const;

    int permutation_[512] {};
};

class PrimitiveWorldStreamer {
public:
    explicit PrimitiveWorldStreamer(PrimitiveWorldConfig config);

    [[nodiscard]] bool update(float cameraX, float cameraZ);
    [[nodiscard]] const std::vector<MEngine::RenderBackend::PrimitiveInstance>& visiblePrimitives() const;
    [[nodiscard]] ChunkCoord centerChunk() const;
    [[nodiscard]] int loadedChunkCount() const;
    [[nodiscard]] const PrimitiveWorldConfig& config() const;

private:
    [[nodiscard]] ChunkCoord chunkFromWorldPosition(float worldX, float worldZ) const;
    [[nodiscard]] bool isInsideWorld(ChunkCoord coord) const;
    void rebuildVisibleChunks(ChunkCoord center);

    PrimitiveWorldConfig config_;
    PrimitiveWorldGenerator generator_;
    ChunkCoord centerChunk_ { 2147483647, 2147483647 };
    int loadedChunkCount_ = 0;
    std::vector<MEngine::RenderBackend::PrimitiveInstance> visiblePrimitives_;
};

} // namespace SandBox
