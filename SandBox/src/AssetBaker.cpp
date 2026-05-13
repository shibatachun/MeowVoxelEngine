#include "MEngine/Core/Log.hpp"
#include "MEngine/Resources/MeshAsset.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

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

void bakeMaterials(const aiScene& scene, MEngine::Resources::MeshAsset& asset)
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
        material.baseColorTexture = texturePath(source, aiTextureType_BASE_COLOR);
        if (material.baseColorTexture.empty()) {
            material.baseColorTexture = texturePath(source, aiTextureType_DIFFUSE);
        }
        material.normalTexture = texturePath(source, aiTextureType_NORMALS);
        material.metallicRoughnessTexture = texturePath(source, aiTextureType_METALNESS);
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

void bakeAnimations(const aiScene& scene, MEngine::Resources::MeshAsset& asset, const std::unordered_map<std::string, uint32_t>& jointMap)
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
                continue;
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
        aiProcess_JoinIdenticalVertices |
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
    bakeMaterials(*scene, asset);
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
              << " animations=" << asset.animations.size() << "\n";
    return 0;
}
