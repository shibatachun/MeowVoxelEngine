#include "MEngine/Core/Log.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct ImagePixels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

glm::mat4 toGlm(const aiMatrix4x4& matrix)
{
    // Assimp exposes row-named fields; GLM stores column-major matrices.
    glm::mat4 result { 1.0f };
    result[0][0] = matrix.a1; result[1][0] = matrix.a2; result[2][0] = matrix.a3; result[3][0] = matrix.a4;
    result[0][1] = matrix.b1; result[1][1] = matrix.b2; result[2][1] = matrix.b3; result[3][1] = matrix.b4;
    result[0][2] = matrix.c1; result[1][2] = matrix.c2; result[2][2] = matrix.c3; result[3][2] = matrix.c4;
    result[0][3] = matrix.d1; result[1][3] = matrix.d2; result[2][3] = matrix.d3; result[3][3] = matrix.d4;
    return result;
}

std::string toString(const aiString& value)
{
    return std::string(value.C_Str());
}

std::string texturePath(const aiMaterial& material, aiTextureType type)
{
    aiString path;
    if (material.GetTexture(type, 0, &path) == AI_SUCCESS) {
        return toString(path);
    }
    return {};
}

std::optional<ImagePixels> loadImagePixels(const std::filesystem::path& path)
{
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.wstring().c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    ImagePixels image;
    hr = converter->GetSize(&image.width, &image.height);
    if (FAILED(hr) || image.width == 0 || image.height == 0) {
        return std::nullopt;
    }

    image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
    const uint32_t stride = image.width * 4;
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(image.rgba.size()), image.rgba.data());
    if (FAILED(hr)) {
        return std::nullopt;
    }

    return image;
}

glm::vec3 sampleImage(const ImagePixels& image, glm::vec2 uv)
{
    uv.x = uv.x - std::floor(uv.x);
    uv.y = uv.y - std::floor(uv.y);

    const uint32_t x = std::min(static_cast<uint32_t>(uv.x * static_cast<float>(image.width)), image.width - 1);
    const uint32_t y = std::min(static_cast<uint32_t>(uv.y * static_cast<float>(image.height)), image.height - 1);
    const size_t offset = (static_cast<size_t>(y) * image.width + x) * 4;
    return {
        static_cast<float>(image.rgba[offset]) / 255.0f,
        static_cast<float>(image.rgba[offset + 1]) / 255.0f,
        static_cast<float>(image.rgba[offset + 2]) / 255.0f,
    };
}

std::string normalizedFilename(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    const size_t slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value = value.substr(slash + 1);
    }
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string resolveBakedTexturePath(
    const std::string& sourcePath,
    const std::unordered_map<std::string, std::string>& bakedTextures)
{
    if (sourcePath.empty()) {
        return {};
    }

    const auto it = bakedTextures.find(normalizedFilename(sourcePath));
    if (it != bakedTextures.end()) {
        return it->second;
    }

    return sourcePath;
}

std::optional<std::filesystem::path> findTextureFile(
    const std::string& sourcePath,
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath)
{
    if (sourcePath.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path source = sourcePath;
    const std::string filename = normalizedFilename(source.filename().string());
    const std::filesystem::path inputDirectory = inputPath.parent_path();
    const std::filesystem::path outputDirectory = outputPath.parent_path();
    const std::filesystem::path bakedTextureDirectory = outputDirectory / (outputPath.stem().string() + "_textures");

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(source);
    candidates.push_back(inputDirectory / source);
    candidates.push_back(outputDirectory / source);
    candidates.push_back(inputDirectory / filename);
    candidates.push_back(outputDirectory / filename);
    candidates.push_back(bakedTextureDirectory / filename);

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::string embedTextureFile(
    const std::string& sourcePath,
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    MEngine::Resources::MeshAsset& asset)
{
    const std::optional<std::filesystem::path> textureFile = findTextureFile(sourcePath, inputPath, outputPath);
    if (!textureFile) {
        return sourcePath;
    }

    const std::string filename = normalizedFilename(textureFile->filename().string());
    const std::filesystem::path textureDirectory = outputPath.parent_path() / (outputPath.stem().string() + "_textures");
    const std::filesystem::path relativeTexturePath = textureDirectory.filename() / filename;
    const std::string relativeName = relativeTexturePath.generic_string();

    const auto duplicateIt = std::find_if(asset.embeddedTextures.begin(), asset.embeddedTextures.end(), [&](const auto& texture) {
        return normalizedFilename(texture.name) == normalizedFilename(relativeName);
    });
    if (duplicateIt != asset.embeddedTextures.end()) {
        return relativeName;
    }

    std::optional<ImagePixels> pixels = loadImagePixels(*textureFile);
    if (!pixels) {
        return sourcePath;
    }

    MEngine::Resources::MeshEmbeddedTexture embedded;
    embedded.name = relativeName;
    embedded.width = pixels->width;
    embedded.height = pixels->height;
    embedded.rgba = std::move(pixels->rgba);
    asset.embeddedTextures.push_back(std::move(embedded));

    std::filesystem::create_directories(textureDirectory);
    std::error_code copyError;
    const std::filesystem::path debugCopyPath = textureDirectory / filename;
    if (!std::filesystem::exists(debugCopyPath)) {
        std::filesystem::copy_file(*textureFile, debugCopyPath, std::filesystem::copy_options::overwrite_existing, copyError);
    }

    return relativeName;
}

void embedMaterialTextures(
    const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath,
    MEngine::Resources::MeshAsset& asset)
{
    for (MEngine::Resources::MeshMaterial& material : asset.materials) {
        material.baseColorTexture = embedTextureFile(material.baseColorTexture, inputPath, outputPath, asset);
        material.normalTexture = embedTextureFile(material.normalTexture, inputPath, outputPath, asset);
        material.metallicRoughnessTexture = embedTextureFile(material.metallicRoughnessTexture, inputPath, outputPath, asset);
    }
}

std::unordered_map<std::string, std::string> extractEmbeddedTextures(
    const aiScene& scene,
    const std::filesystem::path& outputPath,
    MEngine::Resources::MeshAsset& asset)
{
    std::unordered_map<std::string, std::string> bakedTextures;
    if (scene.mNumTextures == 0) {
        return bakedTextures;
    }

    const std::filesystem::path textureDirectory = outputPath.parent_path() / (outputPath.stem().string() + "_textures");
    std::filesystem::create_directories(textureDirectory);

    for (uint32_t textureIndex = 0; textureIndex < scene.mNumTextures; ++textureIndex) {
        const aiTexture& texture = *scene.mTextures[textureIndex];
        std::string filename = texture.mFilename.length > 0
            ? texture.mFilename.C_Str()
            : "embedded_texture_" + std::to_string(textureIndex);
        filename = normalizedFilename(filename);

        const std::string formatHint = texture.achFormatHint[0] != '\0' ? texture.achFormatHint : "png";
        if (std::filesystem::path(filename).extension().empty()) {
            filename += "." + formatHint;
        }

        const std::filesystem::path texturePath = textureDirectory / filename;
        std::ofstream textureStream(texturePath, std::ios::binary);
        if (!textureStream) {
            continue;
        }

        if (texture.mHeight == 0) {
            textureStream.write(reinterpret_cast<const char*>(texture.pcData), texture.mWidth);
        } else {
            textureStream.write(
                reinterpret_cast<const char*>(texture.pcData),
                static_cast<std::streamsize>(texture.mWidth * texture.mHeight * sizeof(aiTexel)));
        }

        const std::filesystem::path relativeTexturePath = textureDirectory.filename() / texturePath.filename();
        const std::string relativeName = relativeTexturePath.generic_string();
        bakedTextures[normalizedFilename(filename)] = relativeName;

        std::optional<ImagePixels> pixels = loadImagePixels(texturePath);
        if (pixels) {
            MEngine::Resources::MeshEmbeddedTexture embedded;
            embedded.name = relativeName;
            embedded.width = pixels->width;
            embedded.height = pixels->height;
            embedded.rgba = std::move(pixels->rgba);
            asset.embeddedTextures.push_back(std::move(embedded));
        }
    }

    return bakedTextures;
}

void bakeBaseColorTexturesToVertices(MEngine::Resources::MeshAsset& asset, const std::filesystem::path& outputPath)
{
    const std::filesystem::path assetDirectory = outputPath.parent_path();
    std::vector<std::optional<ImagePixels>> baseColorTextures(asset.materials.size());

    for (size_t materialIndex = 0; materialIndex < asset.materials.size(); ++materialIndex) {
        const std::string& texturePath = asset.materials[materialIndex].baseColorTexture;
        if (texturePath.empty()) {
            continue;
        }

        std::filesystem::path resolvedPath = texturePath;
        if (resolvedPath.is_relative()) {
            resolvedPath = assetDirectory / resolvedPath;
        }

        baseColorTextures[materialIndex] = loadImagePixels(resolvedPath);
        if (!baseColorTextures[materialIndex]) {
            std::cerr << "Warning: failed to bake base color texture " << resolvedPath.string() << "\n";
        }
    }

    for (const MEngine::Resources::MeshSurface& surface : asset.surfaces) {
        if (surface.materialIndex >= asset.materials.size()) {
            continue;
        }

        const glm::vec3 materialColor = glm::vec3(asset.materials[surface.materialIndex].baseColor);
        const uint32_t endIndex = std::min<uint32_t>(
            surface.firstIndex + surface.indexCount,
            static_cast<uint32_t>(asset.indices.size()));

        for (uint32_t i = surface.firstIndex; i < endIndex; ++i) {
            const uint32_t vertexIndex = asset.indices[i];
            if (vertexIndex >= asset.vertices.size()) {
                continue;
            }

            glm::vec3 color = materialColor;
            if (baseColorTextures[surface.materialIndex]) {
                color *= sampleImage(*baseColorTextures[surface.materialIndex], asset.vertices[vertexIndex].texCoord);
            }
            asset.vertices[vertexIndex].color = color;
        }
    }
}

void addBoneWeight(MEngine::Resources::MeshVertex& vertex, uint32_t boneIndex, float weight)
{
    // Keep only the strongest influences so the runtime skinning path can use
    // a fixed vertex layout across all graphics APIs.
    for (uint32_t slot = 0; slot < MEngine::Resources::MeshMaxBoneInfluences; ++slot) {
        if (vertex.boneWeights[slot] <= 0.0f) {
            vertex.boneIndices[slot] = boneIndex;
            vertex.boneWeights[slot] = weight;
            return;
        }
    }

    uint32_t weakestSlot = 0;
    for (uint32_t slot = 1; slot < MEngine::Resources::MeshMaxBoneInfluences; ++slot) {
        if (vertex.boneWeights[slot] < vertex.boneWeights[weakestSlot]) {
            weakestSlot = slot;
        }
    }

    if (weight > vertex.boneWeights[weakestSlot]) {
        vertex.boneIndices[weakestSlot] = boneIndex;
        vertex.boneWeights[weakestSlot] = weight;
    }
}

void normalizeBoneWeights(MEngine::Resources::MeshVertex& vertex)
{
    float total = vertex.boneWeights.x + vertex.boneWeights.y + vertex.boneWeights.z + vertex.boneWeights.w;
    if (total > 0.0f) {
        vertex.boneWeights /= total;
    }
}

const aiNode* findNode(const aiNode* node, const std::string& name)
{
    if (!node) {
        return nullptr;
    }

    if (name == node->mName.C_Str()) {
        return node;
    }

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        if (const aiNode* found = findNode(node->mChildren[i], name)) {
            return found;
        }
    }
    return nullptr;
}

uint32_t ensureJoint(
    const aiScene& scene,
    const std::string& name,
    const glm::mat4& inverseBindMatrix,
    MEngine::Resources::MeshAsset& asset,
    std::unordered_map<std::string, uint32_t>& jointMap)
{
    // Bone declarations can arrive in mesh order, so this builds the skeleton
    // incrementally while preserving stable indices for animation channels.
    auto it = jointMap.find(name);
    if (it != jointMap.end()) {
        asset.skeleton[it->second].inverseBindMatrix = inverseBindMatrix;
        return it->second;
    }

    MEngine::Resources::SkeletonJoint joint;
    joint.name = name;
    joint.inverseBindMatrix = inverseBindMatrix;
    const uint32_t index = static_cast<uint32_t>(asset.skeleton.size());
    jointMap[name] = index;
    asset.skeleton.push_back(joint);

    if (const aiNode* node = findNode(scene.mRootNode, name)) {
        const aiNode* parent = node->mParent;
        while (parent) {
            // Parent may not be a skinned joint; walk upward until one is known.
            auto parentIt = jointMap.find(parent->mName.C_Str());
            if (parentIt != jointMap.end()) {
                asset.skeleton[index].parentIndex = static_cast<int32_t>(parentIt->second);
                break;
            }
            parent = parent->mParent;
        }
    }

    return index;
}

void bakeMaterials(
    const aiScene& scene,
    MEngine::Resources::MeshAsset& asset,
    const std::unordered_map<std::string, std::string>& bakedTextures)
{
    // Store material data as references and scalar factors; textures are not
    // copied yet, only their source paths are preserved for the asset database.
    asset.materials.reserve(scene.mNumMaterials);
    for (uint32_t i = 0; i < scene.mNumMaterials; ++i) {
        const aiMaterial& source = *scene.mMaterials[i];
        MEngine::Resources::MeshMaterial material;
        aiString name;
        if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            material.name = toString(name);
        }

        aiColor4D baseColor;
        if (source.Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
            source.Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
            material.baseColor = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };
        }
        source.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
        source.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness);
        material.baseColorTexture = resolveBakedTexturePath(texturePath(source, aiTextureType_BASE_COLOR), bakedTextures);
        if (material.baseColorTexture.empty()) {
            material.baseColorTexture = resolveBakedTexturePath(texturePath(source, aiTextureType_DIFFUSE), bakedTextures);
        }
        material.normalTexture = resolveBakedTexturePath(texturePath(source, aiTextureType_NORMALS), bakedTextures);
        material.metallicRoughnessTexture = resolveBakedTexturePath(texturePath(source, aiTextureType_METALNESS), bakedTextures);
        asset.materials.push_back(std::move(material));
    }
}

void bakeMeshes(const aiScene& scene, MEngine::Resources::MeshAsset& asset, std::unordered_map<std::string, uint32_t>& jointMap)
{
    // All imported meshes are packed into one vertex/index buffer. Surfaces keep
    // draw ranges so render code can later batch or material-sort them.
    for (uint32_t meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex) {
        const aiMesh& mesh = *scene.mMeshes[meshIndex];
        const uint32_t vertexBase = static_cast<uint32_t>(asset.vertices.size());

        for (uint32_t i = 0; i < mesh.mNumVertices; ++i) {
            MEngine::Resources::MeshVertex vertex;
            vertex.position = { mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z };
            if (mesh.HasNormals()) {
                vertex.normal = { mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z };
            }
            if (mesh.HasVertexColors(0)) {
                vertex.color = { mesh.mColors[0][i].r, mesh.mColors[0][i].g, mesh.mColors[0][i].b };
            }
            if (mesh.HasTextureCoords(0)) {
                vertex.texCoord = { mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y };
            }
            if (mesh.HasTangentsAndBitangents()) {
                vertex.tangent = { mesh.mTangents[i].x, mesh.mTangents[i].y, mesh.mTangents[i].z, 1.0f };
            }
            asset.vertices.push_back(vertex);
        }

        for (uint32_t boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex) {
            const aiBone& bone = *mesh.mBones[boneIndex];
            const uint32_t jointIndex = ensureJoint(scene, bone.mName.C_Str(), toGlm(bone.mOffsetMatrix), asset, jointMap);
            for (uint32_t weightIndex = 0; weightIndex < bone.mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone.mWeights[weightIndex];
                addBoneWeight(asset.vertices[vertexBase + weight.mVertexId], jointIndex, weight.mWeight);
            }
        }

        for (uint32_t i = vertexBase; i < asset.vertices.size(); ++i) {
            normalizeBoneWeights(asset.vertices[i]);
        }

        MEngine::Resources::MeshSurface surface;
        surface.firstIndex = static_cast<uint32_t>(asset.indices.size());
        surface.materialIndex = mesh.mMaterialIndex;
        for (uint32_t faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
            const aiFace& face = mesh.mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }
            asset.indices.push_back(vertexBase + face.mIndices[0]);
            asset.indices.push_back(vertexBase + face.mIndices[1]);
            asset.indices.push_back(vertexBase + face.mIndices[2]);
        }
        surface.indexCount = static_cast<uint32_t>(asset.indices.size()) - surface.firstIndex;
        asset.surfaces.push_back(surface);
    }
}

void bakeAnimations(const aiScene& scene, MEngine::Resources::MeshAsset& asset, std::unordered_map<std::string, uint32_t>& jointMap)
{
    // Only channels that target baked skeleton joints are kept; unrelated scene
    // node animation can be added later as a separate scene format.
    for (uint32_t animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
        const aiAnimation& source = *scene.mAnimations[animationIndex];
        MEngine::Resources::AnimationClip clip;
        clip.name = source.mName.length > 0 ? source.mName.C_Str() : "Animation";
        clip.ticksPerSecond = source.mTicksPerSecond > 0.0 ? static_cast<float>(source.mTicksPerSecond) : 25.0f;
        clip.durationSeconds = static_cast<float>(source.mDuration / clip.ticksPerSecond);

        for (uint32_t channelIndex = 0; channelIndex < source.mNumChannels; ++channelIndex) {
            const aiNodeAnim& sourceChannel = *source.mChannels[channelIndex];
            auto jointIt = jointMap.find(sourceChannel.mNodeName.C_Str());
            if (jointIt == jointMap.end()) {
                const uint32_t jointIndex = ensureJoint(scene, sourceChannel.mNodeName.C_Str(), glm::mat4(1.0f), asset, jointMap);
                jointIt = jointMap.find(sourceChannel.mNodeName.C_Str());
                if (jointIt == jointMap.end()) {
                    jointIt = jointMap.emplace(sourceChannel.mNodeName.C_Str(), jointIndex).first;
                }
            }

            MEngine::Resources::AnimationChannel channel;
            channel.jointIndex = jointIt->second;
            channel.positions.reserve(sourceChannel.mNumPositionKeys);
            channel.rotations.reserve(sourceChannel.mNumRotationKeys);
            channel.scales.reserve(sourceChannel.mNumScalingKeys);

            for (uint32_t i = 0; i < sourceChannel.mNumPositionKeys; ++i) {
                const aiVectorKey& key = sourceChannel.mPositionKeys[i];
                channel.positions.push_back({
                    static_cast<float>(key.mTime / clip.ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }
            for (uint32_t i = 0; i < sourceChannel.mNumRotationKeys; ++i) {
                const aiQuatKey& key = sourceChannel.mRotationKeys[i];
                channel.rotations.push_back({
                    static_cast<float>(key.mTime / clip.ticksPerSecond),
                    { key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }
            for (uint32_t i = 0; i < sourceChannel.mNumScalingKeys; ++i) {
                const aiVectorKey& key = sourceChannel.mScalingKeys[i];
                channel.scales.push_back({
                    static_cast<float>(key.mTime / clip.ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }

            clip.channels.push_back(std::move(channel));
        }

        asset.animations.push_back(std::move(clip));
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: MeowAssetBaker <input.fbx|obj|gltf|...> <output.mo>\n";
        return 1;
    }

    MEngine::Core::initializeLogging();

    Assimp::Importer importer;
    // Normalize the source model into a renderer-friendly shape before writing .mo.
    const aiScene* scene = importer.ReadFile(
        argv[1],
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_LimitBoneWeights |
        aiProcess_ImproveCacheLocality |
        aiProcess_ValidateDataStructure |
        aiProcess_ConvertToLeftHanded);

    if (!scene || !scene->mRootNode) {
        std::cerr << "Import failed: " << importer.GetErrorString() << "\n";
        return 1;
    }

    MEngine::Resources::MeshAsset asset;
    std::unordered_map<std::string, uint32_t> jointMap;
    const std::filesystem::path inputPath = argv[1];
    const std::filesystem::path outputPath = argv[2];
    const std::unordered_map<std::string, std::string> bakedTextures = extractEmbeddedTextures(*scene, outputPath, asset);
    bakeMaterials(*scene, asset, bakedTextures);
    embedMaterialTextures(inputPath, outputPath, asset);
    bakeMeshes(*scene, asset, jointMap);
    bakeAnimations(*scene, asset, jointMap);

    if (!MEngine::Resources::saveMeshAsset(asset, argv[2])) {
        std::cerr << "Failed to save " << argv[2] << "\n";
        return 1;
    }

    std::cout << "Baked " << argv[1] << " -> " << argv[2]
              << " vertices=" << asset.vertices.size()
              << " indices=" << asset.indices.size()
              << " surfaces=" << asset.surfaces.size()
              << " joints=" << asset.skeleton.size()
              << " animations=" << asset.animations.size()
              << " textures=" << asset.embeddedTextures.size() << "\n";
    return 0;
}
