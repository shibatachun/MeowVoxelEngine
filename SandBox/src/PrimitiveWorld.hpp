#pragma once

#include "MEngine/RenderBackend/Primitive.hpp"
#include "LruCache.hpp"

#include <TaskScheduler.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace SandBox {

struct PrimitiveWorldConfig {
    uint32_t seed = 0;
    int viewDistanceChunks = 3;
    int worldSizeChunks = 4096;
    int chunkSize = 20;
    float cellSize = 0.95f;
    float noiseFrequency = 0.052f;
    float heightScale = 22.0f;
    float waterLine = 0.34f;
    int maxChunkPublishesPerFrame = 2;
    int cachedChunkCapacity = 192;
};

struct ChunkCoord {
    int x = 0;
    int z = 0;

    [[nodiscard]] bool operator==(const ChunkCoord& other) const
    {
        return x == other.x && z == other.z;
    }
};

struct ChunkCoordHash {
    [[nodiscard]] size_t operator()(const ChunkCoord& coord) const
    {
        const uint64_t x = static_cast<uint32_t>(coord.x);
        const uint64_t z = static_cast<uint32_t>(coord.z);
        return static_cast<size_t>((x << 32u) ^ z);
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
    ~PrimitiveWorldStreamer();

    [[nodiscard]] bool update(float cameraX, float cameraZ);
    // Blocks only when the caller explicitly wants a completed world snapshot,
    // currently used for the first frame so startup does not render empty terrain.
    [[nodiscard]] bool waitForPendingLoad();
    [[nodiscard]] const std::vector<MEngine::RenderBackend::PrimitiveInstance>& visiblePrimitives() const;
    [[nodiscard]] ChunkCoord centerChunk() const;
    [[nodiscard]] int loadedChunkCount() const;
    [[nodiscard]] bool hasPendingLoad() const;
    [[nodiscard]] const PrimitiveWorldConfig& config() const;

private:
    [[nodiscard]] ChunkCoord chunkFromWorldPosition(float worldX, float worldZ) const;
    [[nodiscard]] bool isInsideWorld(ChunkCoord coord) const;
    void requestVisibleChunks(ChunkCoord center);
    [[nodiscard]] bool publishCompletedRequest();
    void rebuildVisiblePrimitivesFromCache();

    PrimitiveWorldConfig config_;
    PrimitiveWorldGenerator generator_;
    // CPU terrain generation lives on enkiTS workers; rendering consumes only
    // completed primitive snapshots on the main thread.
    enki::TaskScheduler taskScheduler_;
    class RebuildVisibleChunksTask;
    std::unique_ptr<RebuildVisibleChunksTask> activeTask_;
    ChunkCoord requestedCenterChunk_ { 2147483647, 2147483647 };
    ChunkCoord centerChunk_ { 2147483647, 2147483647 };
    int loadedChunkCount_ = 0;
    LruCache<ChunkCoord, std::vector<MEngine::RenderBackend::PrimitiveInstance>, ChunkCoordHash> loadedChunks_;
    std::vector<ChunkCoord> visibleChunkCoords_;
    std::vector<MEngine::RenderBackend::PrimitiveInstance> visiblePrimitives_;
};

} // namespace SandBox
