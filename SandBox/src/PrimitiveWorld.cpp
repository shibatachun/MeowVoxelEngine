#include "PrimitiveWorld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <utility>

namespace SandBox {

namespace {

float fade(float value)
{
    return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float smoothstep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float grad(int hash, float x, float z)
{
    switch (hash & 7) {
    case 0:
        return x + z;
    case 1:
        return -x + z;
    case 2:
        return x - z;
    case 3:
        return -x - z;
    case 4:
        return x;
    case 5:
        return -x;
    case 6:
        return z;
    default:
        return -z;
    }
}

void setColorForHeight(MEngine::RenderBackend::PrimitiveInstance& primitive, float normalizedHeight)
{
    if (normalizedHeight < 0.30f) {
        primitive.color[0] = 0.30f;
        primitive.color[1] = 0.28f;
        primitive.color[2] = 0.20f;
        return;
    }

    if (normalizedHeight < 0.46f) {
        primitive.color[0] = 0.36f;
        primitive.color[1] = 0.78f;
        primitive.color[2] = 0.32f;
        return;
    }

    if (normalizedHeight < 0.72f) {
        primitive.color[0] = 0.46f;
        primitive.color[1] = 0.56f;
        primitive.color[2] = 0.28f;
        return;
    }

    primitive.color[0] = 0.82f;
    primitive.color[1] = 0.82f;
    primitive.color[2] = 0.78f;
}

float sampleHeight01(const PrimitiveWorldGenerator& generator, const PrimitiveWorldConfig& config, int worldX, int worldZ)
{
    const float sampleX = static_cast<float>(worldX) * config.noiseFrequency;
    const float sampleZ = static_cast<float>(worldZ) * config.noiseFrequency;
    const float continent = generator.fractalNoise(sampleX * 0.36f, sampleZ * 0.36f) * 0.5f + 0.5f;
    const float hills = generator.fractalNoise(sampleX, sampleZ) * 0.5f + 0.5f;
    const float ridges = 1.0f - std::abs(generator.fractalNoise(sampleX * 1.9f + 41.0f, sampleZ * 1.9f - 17.0f));
    const float mountainMask = smoothstep(0.48f, 0.88f, continent);
    const float valleyCut = smoothstep(0.20f, 0.72f, continent);
    const float height = hills * 0.42f + ridges * ridges * mountainMask * 0.55f + valleyCut * 0.18f;
    return std::clamp(height, 0.0f, 1.0f);
}

int sampleBlockHeight(const PrimitiveWorldGenerator& generator, const PrimitiveWorldConfig& config, int worldX, int worldZ)
{
    return static_cast<int>(std::floor((sampleHeight01(generator, config, worldX, worldZ) - config.waterLine) * config.heightScale));
}

} // namespace

struct PrimitiveWorldStreamer::RebuildVisibleChunksTask : enki::ITaskSet {
    struct ChunkResult {
        ChunkCoord coord;
        std::vector<MEngine::RenderBackend::PrimitiveInstance> primitives;
    };

    RebuildVisibleChunksTask(PrimitiveWorldConfig config, PrimitiveWorldGenerator generator, ChunkCoord center)
        : enki::ITaskSet(1)
        , config(std::move(config))
        , generator(std::move(generator))
        , center(center)
    {
        // Precompute the chunk list on the caller thread; worker ranges can then
        // write to unique result slots without locks.
        const int halfWorld = config.worldSizeChunks / 2;
        for (int dz = -config.viewDistanceChunks; dz <= config.viewDistanceChunks; ++dz) {
            for (int dx = -config.viewDistanceChunks; dx <= config.viewDistanceChunks; ++dx) {
                const ChunkCoord coord { center.x + dx, center.z + dz };
                if (coord.x < -halfWorld || coord.x >= halfWorld || coord.z < -halfWorld || coord.z >= halfWorld) {
                    continue;
                }

                chunkCoords.push_back(coord);
            }
        }

        chunkResults.resize(chunkCoords.size());
        // One task item per chunk keeps the async boundary coarse enough to avoid
        // scheduling overhead while still spreading terrain generation.
        m_SetSize = static_cast<uint32_t>(chunkCoords.size());
        m_MinRange = 1;
    }

    RebuildVisibleChunksTask(
        PrimitiveWorldConfig config,
        PrimitiveWorldGenerator generator,
        ChunkCoord center,
        std::vector<ChunkCoord> missingChunks)
        : enki::ITaskSet(1)
        , config(std::move(config))
        , generator(std::move(generator))
        , center(center)
        , chunkCoords(std::move(missingChunks))
    {
        chunkResults.resize(chunkCoords.size());
        m_SetSize = static_cast<uint32_t>(chunkCoords.size());
        m_MinRange = 1;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t) override
    {
        for (uint32_t index = range.start; index < range.end; ++index) {
            chunkResults[index].coord = chunkCoords[index];
            chunkResults[index].primitives = generator.generateChunk(config, chunkCoords[index]);
        }
    }

    PrimitiveWorldConfig config;
    PrimitiveWorldGenerator generator;
    ChunkCoord center;
    std::vector<ChunkCoord> chunkCoords;
    std::vector<ChunkResult> chunkResults;
};

PrimitiveWorldGenerator::PrimitiveWorldGenerator(uint32_t seed)
{
    std::array<int, 256> base {};
    std::iota(base.begin(), base.end(), 0);

    std::mt19937 random(seed);
    std::shuffle(base.begin(), base.end(), random);

    for (int i = 0; i < 512; ++i) {
        permutation_[i] = base[i & 255];
    }
}

std::vector<MEngine::RenderBackend::PrimitiveInstance> PrimitiveWorldGenerator::generate(const PrimitiveWorldConfig& config) const
{
    std::vector<MEngine::RenderBackend::PrimitiveInstance> primitives;
    const int chunkDiameter = config.viewDistanceChunks * 2 + 1;
    primitives.reserve(static_cast<size_t>(chunkDiameter * chunkDiameter * config.chunkSize * config.chunkSize));

    for (int chunkZ = -config.viewDistanceChunks; chunkZ <= config.viewDistanceChunks; ++chunkZ) {
        for (int chunkX = -config.viewDistanceChunks; chunkX <= config.viewDistanceChunks; ++chunkX) {
            std::vector<MEngine::RenderBackend::PrimitiveInstance> chunk = generateChunk(config, { chunkX, chunkZ });
            primitives.insert(primitives.end(), chunk.begin(), chunk.end());
        }
    }

    return primitives;
}

std::vector<MEngine::RenderBackend::PrimitiveInstance> PrimitiveWorldGenerator::generateChunk(
    const PrimitiveWorldConfig& config,
    ChunkCoord coord) const
{
    std::vector<MEngine::RenderBackend::PrimitiveInstance> primitives;
    primitives.reserve(static_cast<size_t>(config.chunkSize * config.chunkSize));

    for (int localZ = 0; localZ < config.chunkSize; ++localZ) {
        for (int localX = 0; localX < config.chunkSize; ++localX) {
            const int worldX = coord.x * config.chunkSize + localX;
            const int worldZ = coord.z * config.chunkSize + localZ;
            const float height01 = sampleHeight01(*this, config, worldX, worldZ);
            const int height = sampleBlockHeight(*this, config, worldX, worldZ);

            MEngine::RenderBackend::PrimitiveInstance primitive {};
            primitive.type = MEngine::RenderBackend::PrimitiveType::Cube;
            primitive.position[0] = static_cast<float>(worldX) * config.cellSize;
            primitive.position[1] = static_cast<float>(height) * config.cellSize;
            primitive.position[2] = static_cast<float>(worldZ) * config.cellSize;
            primitive.size = config.cellSize;
            primitive.visibleFaces = MEngine::RenderBackend::PrimitiveFacePositiveY;
            if (sampleBlockHeight(*this, config, worldX, worldZ - 1) < height) {
                primitive.visibleFaces |= MEngine::RenderBackend::PrimitiveFaceNegativeZ;
            }
            if (sampleBlockHeight(*this, config, worldX, worldZ + 1) < height) {
                primitive.visibleFaces |= MEngine::RenderBackend::PrimitiveFacePositiveZ;
            }
            if (sampleBlockHeight(*this, config, worldX + 1, worldZ) < height) {
                primitive.visibleFaces |= MEngine::RenderBackend::PrimitiveFacePositiveX;
            }
            if (sampleBlockHeight(*this, config, worldX - 1, worldZ) < height) {
                primitive.visibleFaces |= MEngine::RenderBackend::PrimitiveFaceNegativeX;
            }
            setColorForHeight(primitive, height01);
            primitives.push_back(primitive);
        }
    }

    return primitives;
}

float PrimitiveWorldGenerator::fractalNoise(float x, float z) const
{
    float value = 0.0f;
    float amplitude = 0.55f;
    float frequency = 1.0f;
    float normalization = 0.0f;

    for (int octave = 0; octave < 4; ++octave) {
        value += noise(x * frequency, z * frequency) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value / normalization;
}

float PrimitiveWorldGenerator::noise(float x, float z) const
{
    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int zi = static_cast<int>(std::floor(z)) & 255;
    const float xf = x - std::floor(x);
    const float zf = z - std::floor(z);
    const float u = fade(xf);
    const float v = fade(zf);

    const int aa = permutation_[permutation_[xi] + zi];
    const int ab = permutation_[permutation_[xi] + zi + 1];
    const int ba = permutation_[permutation_[xi + 1] + zi];
    const int bb = permutation_[permutation_[xi + 1] + zi + 1];

    const float x1 = lerp(grad(aa, xf, zf), grad(ba, xf - 1.0f, zf), u);
    const float x2 = lerp(grad(ab, xf, zf - 1.0f), grad(bb, xf - 1.0f, zf - 1.0f), u);
    return std::clamp(lerp(x1, x2, v), -1.0f, 1.0f);
}

PrimitiveWorldStreamer::PrimitiveWorldStreamer(PrimitiveWorldConfig config)
    : config_(config)
    , generator_(config.seed)
    , loadedChunks_(static_cast<size_t>(std::max(config.cachedChunkCapacity, 1)))
{
    taskScheduler_.Initialize();
}

PrimitiveWorldStreamer::~PrimitiveWorldStreamer()
{
    if (activeTask_) {
        taskScheduler_.WaitforTask(activeTask_.get());
        activeTask_.reset();
    }
    taskScheduler_.WaitforAllAndShutdown();
}

bool PrimitiveWorldStreamer::update(float cameraX, float cameraZ)
{
    requestedCenterChunk_ = chunkFromWorldPosition(cameraX, cameraZ);

    const bool publishedCompletedRequest = publishCompletedRequest();
    // Keep the old visible terrain until the new center chunk finishes loading.
    if (!activeTask_ && !(requestedCenterChunk_ == centerChunk_)) {
        requestVisibleChunks(requestedCenterChunk_);
    }

    return publishedCompletedRequest;
}

bool PrimitiveWorldStreamer::waitForPendingLoad()
{
    if (!activeTask_) {
        return false;
    }

    taskScheduler_.WaitforTask(activeTask_.get());
    bool published = false;
    while (publishCompletedRequest()) {
        published = true;
    }
    return published;
}

const std::vector<MEngine::RenderBackend::PrimitiveInstance>& PrimitiveWorldStreamer::visiblePrimitives() const
{
    return visiblePrimitives_;
}

ChunkCoord PrimitiveWorldStreamer::centerChunk() const
{
    return centerChunk_;
}

int PrimitiveWorldStreamer::loadedChunkCount() const
{
    return loadedChunkCount_;
}

bool PrimitiveWorldStreamer::hasPendingLoad() const
{
    return activeTask_ != nullptr;
}

const PrimitiveWorldConfig& PrimitiveWorldStreamer::config() const
{
    return config_;
}

ChunkCoord PrimitiveWorldStreamer::chunkFromWorldPosition(float worldX, float worldZ) const
{
    const float chunkWorldSize = static_cast<float>(config_.chunkSize) * config_.cellSize;
    return {
        static_cast<int>(std::floor(worldX / chunkWorldSize)),
        static_cast<int>(std::floor(worldZ / chunkWorldSize))
    };
}

bool PrimitiveWorldStreamer::isInsideWorld(ChunkCoord coord) const
{
    const int halfWorld = config_.worldSizeChunks / 2;
    return coord.x >= -halfWorld && coord.x < halfWorld && coord.z >= -halfWorld && coord.z < halfWorld;
}

void PrimitiveWorldStreamer::requestVisibleChunks(ChunkCoord center)
{
    const int halfWorld = config_.worldSizeChunks / 2;
    visibleChunkCoords_.clear();
    std::vector<ChunkCoord> missingChunks;

    for (int dz = -config_.viewDistanceChunks; dz <= config_.viewDistanceChunks; ++dz) {
        for (int dx = -config_.viewDistanceChunks; dx <= config_.viewDistanceChunks; ++dx) {
            const ChunkCoord coord { center.x + dx, center.z + dz };
            if (coord.x < -halfWorld || coord.x >= halfWorld || coord.z < -halfWorld || coord.z >= halfWorld) {
                continue;
            }

            visibleChunkCoords_.push_back(coord);
            if (!loadedChunks_.find(coord)) {
                missingChunks.push_back(coord);
            }
        }
    }

    for (const ChunkCoord& coord : visibleChunkCoords_) {
        loadedChunks_.touch(coord);
    }

    centerChunk_ = center;
    requestedCenterChunk_ = center;
    if (missingChunks.empty()) {
        rebuildVisiblePrimitivesFromCache();
        return;
    }

    activeTask_ = std::make_unique<RebuildVisibleChunksTask>(config_, generator_, center, std::move(missingChunks));
    if (activeTask_->m_SetSize == 0) {
        rebuildVisiblePrimitivesFromCache();
        centerChunk_ = center;
        activeTask_.reset();
        return;
    }

    taskScheduler_.AddTaskSetToPipe(activeTask_.get());
}

bool PrimitiveWorldStreamer::publishCompletedRequest()
{
    if (!activeTask_ || !activeTask_->GetIsComplete()) {
        return false;
    }

    taskScheduler_.WaitforTask(activeTask_.get());
    if (!(activeTask_->center == centerChunk_)) {
        // Camera moved again while this task was running; discard stale terrain
        // and let update() request the current center on the next pass.
        activeTask_.reset();
        return false;
    }

    const int publishBudget = std::max(config_.maxChunkPublishesPerFrame, 1);
    int publishedChunks = 0;
    while (!activeTask_->chunkResults.empty() && publishedChunks < publishBudget) {
        RebuildVisibleChunksTask::ChunkResult& result = activeTask_->chunkResults.back();
        loadedChunks_.put(result.coord, std::move(result.primitives));
        activeTask_->chunkResults.pop_back();
        ++publishedChunks;
    }

    if (activeTask_->chunkResults.empty()) {
        activeTask_.reset();
        rebuildVisiblePrimitivesFromCache();
    }

    return true;
}

void PrimitiveWorldStreamer::rebuildVisiblePrimitivesFromCache()
{
    size_t primitiveCount = 0;
    loadedChunkCount_ = 0;
    for (const ChunkCoord& coord : visibleChunkCoords_) {
        const auto* chunk = loadedChunks_.find(coord);
        if (!chunk) {
            continue;
        }

        primitiveCount += chunk->size();
        ++loadedChunkCount_;
    }

    std::vector<MEngine::RenderBackend::PrimitiveInstance> visiblePrimitives;
    visiblePrimitives.reserve(primitiveCount);
    for (const ChunkCoord& coord : visibleChunkCoords_) {
        const auto* chunk = loadedChunks_.find(coord);
        if (!chunk) {
            continue;
        }

        visiblePrimitives.insert(visiblePrimitives.end(), chunk->begin(), chunk->end());
    }

    visiblePrimitives_ = std::move(visiblePrimitives);
}

} // namespace SandBox
