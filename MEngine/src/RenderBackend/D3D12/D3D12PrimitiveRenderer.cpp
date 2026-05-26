#include "D3D12PrimitiveRenderer.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace MEngine::RenderBackend::D3D12 {

namespace {

void appendFace(
    std::vector<D3D12PrimitiveRenderer::PrimitiveVertex>& vertices,
    std::vector<uint32_t>& indices,
    const std::array<std::array<float, 3>, 8>& corners,
    std::array<uint32_t, 4> face,
    std::array<float, 3> normal,
    std::array<float, 3> color)
{
    const uint32_t faceBase = static_cast<uint32_t>(vertices.size());
    const std::array<std::array<float, 2>, 4> uvs {{
        {{ 0.0f, 0.0f }},
        {{ 1.0f, 0.0f }},
        {{ 1.0f, 1.0f }},
        {{ 0.0f, 1.0f }},
    }};

    for (size_t i = 0; i < face.size(); ++i) {
        const auto& position = corners[face[i]];
        vertices.push_back({
            { position[0], position[1], position[2] },
            { normal[0], normal[1], normal[2] },
            { color[0], color[1], color[2] },
            { uvs[i][0], uvs[i][1] },
        });
    }

    indices.insert(indices.end(), { faceBase, faceBase + 2, faceBase + 1, faceBase, faceBase + 3, faceBase + 2 });
}

void appendCube(
    std::vector<D3D12PrimitiveRenderer::PrimitiveVertex>& vertices,
    std::vector<uint32_t>& indices,
    const PrimitiveInstance& primitive)
{
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const std::array<float, 3> color { primitive.color[0], primitive.color[1], primitive.color[2] };

    const std::array<std::array<float, 3>, 8> corners {{
        {{ cx - half, cy - half, cz - half }},
        {{ cx + half, cy - half, cz - half }},
        {{ cx + half, cy + half, cz - half }},
        {{ cx - half, cy + half, cz - half }},
        {{ cx - half, cy - half, cz + half }},
        {{ cx + half, cy - half, cz + half }},
        {{ cx + half, cy + half, cz + half }},
        {{ cx - half, cy + half, cz + half }},
    }};

    if ((primitive.visibleFaces & PrimitiveFaceNegativeZ) != 0) {
        appendFace(vertices, indices, corners, { 0, 1, 2, 3 }, { 0.0f, 0.0f, -1.0f }, color);
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveZ) != 0) {
        appendFace(vertices, indices, corners, { 4, 7, 6, 5 }, { 0.0f, 0.0f, 1.0f }, color);
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeY) != 0) {
        appendFace(vertices, indices, corners, { 0, 4, 5, 1 }, { 0.0f, -1.0f, 0.0f }, color);
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveY) != 0) {
        appendFace(vertices, indices, corners, { 3, 2, 6, 7 }, { 0.0f, 1.0f, 0.0f }, color);
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveX) != 0) {
        appendFace(vertices, indices, corners, { 1, 5, 6, 2 }, { 1.0f, 0.0f, 0.0f }, color);
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeX) != 0) {
        appendFace(vertices, indices, corners, { 0, 3, 7, 4 }, { -1.0f, 0.0f, 0.0f }, color);
    }
}

void appendTriangle(
    std::vector<D3D12PrimitiveRenderer::PrimitiveVertex>& vertices,
    std::vector<uint32_t>& indices,
    const PrimitiveInstance& primitive)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    vertices.push_back({ { cx, cy + half, cz }, { 0.0f, 0.9f, 0.2f }, { r, g, b }, { 0.5f, 1.0f } });
    vertices.push_back({ { cx - half, cy - half, cz + half }, { -0.6f, -0.45f, 0.6f }, { r, g, b }, { 0.0f, 0.0f } });
    vertices.push_back({ { cx + half, cy - half, cz + half }, { 0.6f, -0.45f, 0.6f }, { r, g, b }, { 1.0f, 0.0f } });
    vertices.push_back({ { cx, cy - half, cz - half }, { 0.0f, -0.45f, -0.9f }, { r, g, b }, { 0.5f, 0.0f } });
    indices.insert(indices.end(), {
        base, base + 1, base + 2,
        base, base + 2, base + 3,
        base, base + 3, base + 1,
        base + 1, base + 3, base + 2,
    });
}

void appendPrimitive(
    std::vector<D3D12PrimitiveRenderer::PrimitiveVertex>& vertices,
    std::vector<uint32_t>& indices,
    const PrimitiveInstance& primitive)
{
    switch (primitive.type) {
    case PrimitiveType::Triangle:
        appendTriangle(vertices, indices, primitive);
        break;
    case PrimitiveType::Quad:
    case PrimitiveType::Cube:
    case PrimitiveType::Sphere:
        appendCube(vertices, indices, primitive);
        break;
    }
}

} // namespace

void D3D12PrimitiveRenderer::initialize(
    ID3D12Device* device,
    DXGI_FORMAT backBufferFormat,
    DXGI_FORMAT depthBufferFormat,
    const std::filesystem::path& shaderDirectory)
{
    device_ = device;

    D3D12_ROOT_PARAMETER rootParameter {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.Num32BitValues = sizeof(CameraConstants) / sizeof(uint32_t);
    rootParameter.Constants.ShaderRegister = 0;
    rootParameter.Constants.RegisterSpace = 0;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc {};
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &rootParameter;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;
    const HRESULT serializeResult = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSignature,
        &errorBlob);
    if (FAILED(serializeResult)) {
        const char* errorText = errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "unknown error";
        throw std::runtime_error(std::string("D3D12SerializeRootSignature failed: ") + errorText);
    }

    checkHResult(device_->CreateRootSignature(
                     0,
                     serializedRootSignature->GetBufferPointer(),
                     serializedRootSignature->GetBufferSize(),
                     IID_PPV_ARGS(&rootSignature_)),
        "ID3D12Device::CreateRootSignature");

    const std::vector<uint8_t> vertexShader = readBinaryFile(shaderDirectory / "ForwardPrimitive.vs.cso");
    const std::vector<uint8_t> pixelShader = readBinaryFile(shaderDirectory / "ForwardPrimitive.ps.cso");

    const D3D12_INPUT_ELEMENT_DESC inputElements[] {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PrimitiveVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PrimitiveVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(PrimitiveVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PrimitiveVertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = { vertexShader.data(), vertexShader.size() };
    psoDesc.PS = { pixelShader.data(), pixelShader.size() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.InputLayout = { inputElements, static_cast<UINT>(sizeof(inputElements) / sizeof(inputElements[0])) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = backBufferFormat;
    psoDesc.DSVFormat = depthBufferFormat;
    psoDesc.SampleDesc.Count = 1;

    checkHResult(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_)),
        "ID3D12Device::CreateGraphicsPipelineState primitive");
}

void D3D12PrimitiveRenderer::shutdown()
{
    staticBuffers_ = {};
    dynamicBuffers_ = {};
    pipelineState_ = nullptr;
    rootSignature_ = nullptr;
    device_ = nullptr;
}

void D3D12PrimitiveRenderer::setStaticPrimitives(const std::vector<PrimitiveInstance>& primitives)
{
    rebuildBuffers(staticBuffers_, primitives, false);
}

void D3D12PrimitiveRenderer::setDynamicPrimitives(const std::vector<PrimitiveInstance>& primitives)
{
    rebuildBuffers(dynamicBuffers_, primitives, true);
}

void D3D12PrimitiveRenderer::uploadPendingBuffers(ID3D12GraphicsCommandList* commandList)
{
    uploadBufferSet(commandList, staticBuffers_);
    uploadBufferSet(commandList, dynamicBuffers_);
    if (retiredBuffers_.size() > 6) {
        retiredBuffers_.erase(retiredBuffers_.begin(), retiredBuffers_.begin() + static_cast<std::ptrdiff_t>(retiredBuffers_.size() - 6));
    }
}

void D3D12PrimitiveRenderer::draw(ID3D12GraphicsCommandList* commandList, const Camera::CameraState* camera, uint32_t width, uint32_t height)
{
    if (!pipelineState_ || !rootSignature_) {
        return;
    }

    uploadPendingBuffers(commandList);

    const CameraConstants constants = makeCameraConstants(camera, width, height);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->SetGraphicsRoot32BitConstants(
        0,
        static_cast<UINT>(sizeof(CameraConstants) / sizeof(uint32_t)),
        &constants,
        0);

    D3D12_VIEWPORT viewport {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    drawBufferSet(commandList, staticBuffers_);
    drawBufferSet(commandList, dynamicBuffers_);
}

size_t D3D12PrimitiveRenderer::primitiveCount() const
{
    return staticBuffers_.primitives.size() + dynamicBuffers_.primitives.size();
}

size_t D3D12PrimitiveRenderer::indexCount() const
{
    return staticBuffers_.indices.size() + dynamicBuffers_.indices.size();
}

void D3D12PrimitiveRenderer::rebuildBuffers(PrimitiveBufferSet& buffers, const std::vector<PrimitiveInstance>& primitives, bool keepOldGpuResourcesAlive)
{
    if (keepOldGpuResourcesAlive) {
        retireBuffers(std::move(buffers));
    }

    buffers.primitives = primitives;
    buffers.vertices.clear();
    buffers.indices.clear();
    buffers.vertexBuffer = nullptr;
    buffers.indexBuffer = nullptr;
    buffers.vertexUploadBuffer = nullptr;
    buffers.indexUploadBuffer = nullptr;
    buffers.vertexBufferView = {};
    buffers.indexBufferView = {};
    buffers.needsUpload = false;

    if (primitives.empty() || !device_) {
        return;
    }

    buffers.vertices.reserve(primitives.size() * 24);
    buffers.indices.reserve(primitives.size() * 36);
    for (const PrimitiveInstance& primitive : primitives) {
        appendPrimitive(buffers.vertices, buffers.indices, primitive);
    }

    if (buffers.vertices.empty() || buffers.indices.empty()) {
        return;
    }

    const UINT64 vertexBytes = buffers.vertices.size() * sizeof(PrimitiveVertex);
    const UINT64 indexBytes = buffers.indices.size() * sizeof(uint32_t);
    buffers.vertexBuffer = createBuffer(device_, vertexBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, "primitive vertex buffer");
    buffers.indexBuffer = createBuffer(device_, indexBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, "primitive index buffer");
    buffers.vertexUploadBuffer = createBuffer(device_, vertexBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, "primitive vertex upload buffer");
    buffers.indexUploadBuffer = createBuffer(device_, indexBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, "primitive index upload buffer");

    void* mappedData = nullptr;
    D3D12_RANGE readRange { 0, 0 };
    checkHResult(buffers.vertexUploadBuffer->Map(0, &readRange, &mappedData), "ID3D12Resource::Map primitive vertex upload buffer");
    std::memcpy(mappedData, buffers.vertices.data(), static_cast<size_t>(vertexBytes));
    buffers.vertexUploadBuffer->Unmap(0, nullptr);

    checkHResult(buffers.indexUploadBuffer->Map(0, &readRange, &mappedData), "ID3D12Resource::Map primitive index upload buffer");
    std::memcpy(mappedData, buffers.indices.data(), static_cast<size_t>(indexBytes));
    buffers.indexUploadBuffer->Unmap(0, nullptr);

    buffers.vertexBufferView.BufferLocation = buffers.vertexBuffer->GetGPUVirtualAddress();
    buffers.vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
    buffers.vertexBufferView.StrideInBytes = sizeof(PrimitiveVertex);
    buffers.indexBufferView.BufferLocation = buffers.indexBuffer->GetGPUVirtualAddress();
    buffers.indexBufferView.SizeInBytes = static_cast<UINT>(indexBytes);
    buffers.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    buffers.needsUpload = true;
}

void D3D12PrimitiveRenderer::retireBuffers(PrimitiveBufferSet&& buffers)
{
    if (!buffers.vertexBuffer && !buffers.indexBuffer && !buffers.vertexUploadBuffer && !buffers.indexUploadBuffer) {
        return;
    }

    buffers.primitives.clear();
    buffers.vertices.clear();
    buffers.indices.clear();
    retiredBuffers_.push_back(std::move(buffers));
}

void D3D12PrimitiveRenderer::uploadBufferSet(ID3D12GraphicsCommandList* commandList, PrimitiveBufferSet& buffers)
{
    if (!buffers.needsUpload || !buffers.vertexUploadBuffer || !buffers.indexUploadBuffer) {
        return;
    }

    commandList->CopyBufferRegion(buffers.vertexBuffer.Get(), 0, buffers.vertexUploadBuffer.Get(), 0, buffers.vertices.size() * sizeof(PrimitiveVertex));
    commandList->CopyBufferRegion(buffers.indexBuffer.Get(), 0, buffers.indexUploadBuffer.Get(), 0, buffers.indices.size() * sizeof(uint32_t));

    const D3D12_RESOURCE_BARRIER barriers[] {
        transitionBarrier(buffers.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        transitionBarrier(buffers.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList->ResourceBarrier(static_cast<UINT>(sizeof(barriers) / sizeof(barriers[0])), barriers);
    buffers.needsUpload = false;
}

void D3D12PrimitiveRenderer::drawBufferSet(ID3D12GraphicsCommandList* commandList, const PrimitiveBufferSet& buffers)
{
    if (!buffers.vertexBuffer || !buffers.indexBuffer || buffers.indices.empty() || buffers.needsUpload) {
        return;
    }

    commandList->IASetVertexBuffers(0, 1, &buffers.vertexBufferView);
    commandList->IASetIndexBuffer(&buffers.indexBufferView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(buffers.indices.size()), 1, 0, 0, 0);
}

D3D12PrimitiveRenderer::CameraConstants D3D12PrimitiveRenderer::makeCameraConstants(
    const Camera::CameraState* camera,
    uint32_t width,
    uint32_t height) const
{
    Camera::CameraState fallbackCamera {};
    const Camera::CameraState& activeCamera = camera ? *camera : fallbackCamera;
    const glm::vec3 eye { activeCamera.position[0], activeCamera.position[1], activeCamera.position[2] };
    const glm::vec3 target { activeCamera.target[0], activeCamera.target[1], activeCamera.target[2] };
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(activeCamera.fovDegrees),
        static_cast<float>((std::max)(width, 1u)) / static_cast<float>((std::max)(height, 1u)),
        activeCamera.nearPlane,
        activeCamera.farPlane);

    CameraConstants constants {};
    constants.viewProjection = projection * view;
    constants.lightDirection = glm::vec4(glm::normalize(glm::vec3(-0.35f, -0.72f, -0.6f)), 0.0f);
    return constants;
}

} // namespace MEngine::RenderBackend::D3D12
