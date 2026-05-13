#include "PrimitiveWorld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>

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
    if (normalizedHeight < 0.28f) {
        primitive.color[0] = 0.18f;
        primitive.color[1] = 0.42f;
        primitive.color[2] = 0.95f;
        return;
    }

    if (normalizedHeight < 0.42f) {
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
    return std::clamp(generator.fractalNoise(sampleX, sampleZ) * 0.5f + 0.5f, 0.0f, 1.0f);
}

int sampleBlockHeight(const PrimitiveWorldGenerator& generator, const PrimitiveWorldConfig& config, int worldX, int worldZ)
{
    return static_cast<int>(std::floor(sampleHeight01(generator, config, worldX, worldZ) * config.heightScale));
}

} // namespace

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
{
}

bool PrimitiveWorldStreamer::update(float cameraX, float cameraZ)
{
    const ChunkCoord center = chunkFromWorldPosition(cameraX, cameraZ);
    if (center == centerChunk_) {
        return false;
    }

    rebuildVisibleChunks(center);
    return true;
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

void PrimitiveWorldStreamer::rebuildVisibleChunks(ChunkCoord center)
{
    visiblePrimitives_.clear();
    loadedChunkCount_ = 0;
    centerChunk_ = center;

    const int chunkDiameter = config_.viewDistanceChunks * 2 + 1;
    visiblePrimitives_.reserve(static_cast<size_t>(
        chunkDiameter * chunkDiameter * config_.chunkSize * config_.chunkSize));

    for (int dz = -config_.viewDistanceChunks; dz <= config_.viewDistanceChunks; ++dz) {
        for (int dx = -config_.viewDistanceChunks; dx <= config_.viewDistanceChunks; ++dx) {
            const ChunkCoord coord { center.x + dx, center.z + dz };
            if (!isInsideWorld(coord)) {
                continue;
            }

            std::vector<MEngine::RenderBackend::PrimitiveInstance> chunk = generator_.generateChunk(config_, coord);
            visiblePrimitives_.insert(visiblePrimitives_.end(), chunk.begin(), chunk.end());
            ++loadedChunkCount_;
        }
    }
}

} // namespace SandBox
