#pragma once

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/RenderBackend/Primitive.hpp"

#include "D3D12Utils.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace MEngine::RenderBackend::D3D12 {

class D3D12PrimitiveRenderer {
public:
    void initialize(
        ID3D12Device* device,
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthBufferFormat,
        const std::filesystem::path& shaderDirectory);
    void shutdown();

    void setStaticPrimitives(const std::vector<PrimitiveInstance>& primitives);
    void setDynamicPrimitives(const std::vector<PrimitiveInstance>& primitives);
    void uploadPendingBuffers(ID3D12GraphicsCommandList* commandList);
    void draw(ID3D12GraphicsCommandList* commandList, const Camera::CameraState* camera, uint32_t width, uint32_t height);

    [[nodiscard]] size_t primitiveCount() const;
    [[nodiscard]] size_t indexCount() const;

    struct PrimitiveVertex {
        float position[3];
        float normal[3];
        float color[3];
        float uv[2];
    };

    struct CameraConstants {
        glm::mat4 viewProjection { 1.0f };
        glm::vec4 lightDirection { -0.35f, -0.72f, -0.6f, 0.0f };
    };

    struct PrimitiveBufferSet {
        std::vector<PrimitiveInstance> primitives;
        std::vector<PrimitiveVertex> vertices;
        std::vector<uint32_t> indices;
        ComPtr<ID3D12Resource> vertexBuffer;
        ComPtr<ID3D12Resource> indexBuffer;
        ComPtr<ID3D12Resource> vertexUploadBuffer;
        ComPtr<ID3D12Resource> indexUploadBuffer;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView {};
        D3D12_INDEX_BUFFER_VIEW indexBufferView {};
        bool needsUpload = false;
    };

private:
    void rebuildBuffers(PrimitiveBufferSet& buffers, const std::vector<PrimitiveInstance>& primitives, bool keepOldGpuResourcesAlive);
    void retireBuffers(PrimitiveBufferSet&& buffers);
    void uploadBufferSet(ID3D12GraphicsCommandList* commandList, PrimitiveBufferSet& buffers);
    void drawBufferSet(ID3D12GraphicsCommandList* commandList, const PrimitiveBufferSet& buffers);
    [[nodiscard]] CameraConstants makeCameraConstants(const Camera::CameraState* camera, uint32_t width, uint32_t height) const;

    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    PrimitiveBufferSet staticBuffers_;
    PrimitiveBufferSet dynamicBuffers_;
    std::vector<PrimitiveBufferSet> retiredBuffers_;
};

} // namespace MEngine::RenderBackend::D3D12
