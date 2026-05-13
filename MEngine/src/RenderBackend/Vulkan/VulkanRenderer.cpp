#include "MEngine/RenderBackend/Vulkan/VulkanRenderer.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanShader.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanSwapchain.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

#ifndef MENGINE_SHADER_BINARY_DIR
#define MENGINE_SHADER_BINARY_DIR "."
#endif

namespace MEngine::RenderBackend::Vulkan {

namespace {

constexpr uint32_t MaxPrimitiveVertices = 524288;
constexpr uint32_t MaxPrimitiveIndices = 1048576;
constexpr float Pi = 3.14159265358979323846f;

struct Mat4 {
    float m[16] {};
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 subtract(Vec3 a, Vec3 b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.00001f) {
        return { 0.0f, 0.0f, 1.0f };
    }

    return { value.x / length, value.y / length, value.z / length };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
            result.m[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane)
{
    const float tanHalfFov = std::tan(fovRadians * 0.5f);

    Mat4 result {};
    result.m[0] = 1.0f / (aspect * tanHalfFov);
    result.m[5] = -1.0f / tanHalfFov;
    result.m[10] = farPlane / (nearPlane - farPlane);
    result.m[11] = -1.0f;
    result.m[14] = -(farPlane * nearPlane) / (farPlane - nearPlane);
    return result;
}

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 forward = normalize(subtract(target, eye));
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 cameraUp = cross(right, forward);

    Mat4 result {};
    result.m[0] = right.x;
    result.m[1] = cameraUp.x;
    result.m[2] = -forward.x;
    result.m[4] = right.y;
    result.m[5] = cameraUp.y;
    result.m[6] = -forward.y;
    result.m[8] = right.z;
    result.m[9] = cameraUp.z;
    result.m[10] = -forward.z;
    result.m[12] = -dot(right, eye);
    result.m[13] = -dot(cameraUp, eye);
    result.m[14] = dot(forward, eye);
    result.m[15] = 1.0f;
    return result;
}

const char* primitiveName(BasicPrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Triangle:
        return "Triangle";
    case PrimitiveType::Quad:
        return "Quad";
    case PrimitiveType::Cube:
        return "Cube";
    case PrimitiveType::Sphere:
        return "Sphere";
    }

    return "Unknown";
}

} // namespace

VulkanRenderer::~VulkanRenderer()
{
    shutdown();
}

void VulkanRenderer::initialize(VulkanDevice& device, VulkanSwapchain& swapchain, SDL_Window* window)
{
    device_ = &device;
    swapchain_ = &swapchain;

    createFramebuffers(swapchain);
    createShaders();
    createPipeline();
    createBuffers();

    if (primitives_.empty()) {
        addPrimitive(PrimitiveType::Cube);
    }

    imguiLayer_ = std::make_unique<VulkanImGuiLayer>();
    imguiLayer_->initialize(device, swapchain, framebuffers_.front(), window);
    imguiLayer_->setPanelCallback([this]() { drawPrimitivePanel(); });

    commandList_ = device_->nvrhiDevice()->createCommandList();
    if (!commandList_) {
        throw std::runtime_error("Failed to create NVRHI Vulkan command list");
    }

    MENGINE_INFO("[RenderBackend] Vulkan renderer initialized via NVRHI command list");
}

void VulkanRenderer::setCameraState(const Camera::CameraState& camera)
{
    camera_ = camera;
}

void VulkanRenderer::recreateSwapchainResources(VulkanSwapchain& swapchain, SDL_Window* window)
{
    releaseGpuResources();
    swapchain_ = &swapchain;
    createFramebuffers(swapchain);
    createShaders();
    createPipeline();
    createBuffers();

    imguiLayer_ = std::make_unique<VulkanImGuiLayer>();
    imguiLayer_->initialize(*device_, swapchain, framebuffers_.front(), window);
    imguiLayer_->setPanelCallback([this]() { drawPrimitivePanel(); });

    commandList_ = device_->nvrhiDevice()->createCommandList();
    if (!commandList_) {
        throw std::runtime_error("Failed to recreate NVRHI Vulkan command list");
    }

    meshDirty_ = true;
}

uint64_t VulkanRenderer::render(uint32_t imageIndex)
{
    if (!device_ || imageIndex >= framebuffers_.size()) {
        return 0;
    }

    nvrhi::ITexture* target = swapchain_->nvrhiImages()[imageIndex];
    nvrhi::IFramebuffer* framebuffer = framebuffers_[imageIndex];

    commandList_->open();
    commandList_->beginMarker("MeowEngine Renderer");
    commandList_->setTextureState(gBufferPosition_, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
    commandList_->setTextureState(gBufferNormal_, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
    commandList_->setTextureState(gBufferAlbedo_, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
    commandList_->setTextureState(gBufferMaterial_, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
    commandList_->setTextureState(depthTexture_, nvrhi::AllSubresources, nvrhi::ResourceStates::DepthWrite);
    commandList_->commitBarriers();
    commandList_->clearTextureFloat(gBufferPosition_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    commandList_->clearTextureFloat(gBufferNormal_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 1.0f, 0.0f, 0.0f));
    commandList_->clearTextureFloat(gBufferAlbedo_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    commandList_->clearTextureFloat(gBufferMaterial_, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.5f, 0.03f, 0.0f));
    commandList_->clearDepthStencilTexture(depthTexture_, nvrhi::AllSubresources, true, 1.0f, false, 0);

    if (meshDirty_) {
        rebuildMesh();
        meshDirty_ = false;
        gpuMeshDirty_ = true;
    }

    if (gpuMeshDirty_ && !vertices_.empty() && !indices_.empty()) {
        commandList_->writeBuffer(vertexBuffer_, vertices_.data(), vertices_.size() * sizeof(Vertex));
        commandList_->writeBuffer(indexBuffer_, indices_.data(), indices_.size() * sizeof(uint32_t));
        uploadedIndexCount_ = static_cast<uint32_t>(indices_.size());
        gpuMeshDirty_ = false;
    }

    if (uploadedIndexCount_ > 0) {
        nvrhi::ViewportState viewportState;
        viewportState.addViewportAndScissorRect(nvrhi::Viewport(
            static_cast<float>(swapchain_->extent().width),
            static_cast<float>(swapchain_->extent().height)));

        nvrhi::GraphicsState graphicsState;
        graphicsState.setPipeline(geometryPipeline_);
        graphicsState.setFramebuffer(gBufferFramebuffer_);
        graphicsState.setViewport(viewportState);
        graphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vertexBuffer_).setSlot(0).setOffset(0));
        graphicsState.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(indexBuffer_).setFormat(nvrhi::Format::R32_UINT).setOffset(0));

        commandList_->setBufferState(vertexBuffer_, nvrhi::ResourceStates::VertexBuffer);
        commandList_->setBufferState(indexBuffer_, nvrhi::ResourceStates::IndexBuffer);
        commandList_->commitBarriers();
        commandList_->setGraphicsState(graphicsState);
        const PushConstants pushConstants = buildPushConstants();
        commandList_->setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList_->drawIndexed(nvrhi::DrawArguments().setVertexCount(uploadedIndexCount_));
    }

    const LightingConstants lightingConstants = buildLightingConstants();
    commandList_->writeBuffer(lightingConstantsBuffer_, &lightingConstants, sizeof(lightingConstants));
    commandList_->setBufferState(lightingConstantsBuffer_, nvrhi::ResourceStates::ConstantBuffer);
    commandList_->setTextureState(target, nvrhi::AllSubresources, nvrhi::ResourceStates::RenderTarget);
    commandList_->commitBarriers();

    nvrhi::ViewportState fullscreenViewport;
    fullscreenViewport.addViewportAndScissorRect(nvrhi::Viewport(
        static_cast<float>(swapchain_->extent().width),
        static_cast<float>(swapchain_->extent().height)));

    nvrhi::GraphicsState skyState;
    skyState.setPipeline(skyAtmospherePipeline_);
    skyState.setFramebuffer(framebuffer);
    skyState.setViewport(fullscreenViewport);
    skyState.addBindingSet(skyAtmosphereBindingSet_);

    commandList_->setResourceStatesForBindingSet(skyAtmosphereBindingSet_);
    commandList_->commitBarriers();
    commandList_->setGraphicsState(skyState);
    commandList_->draw(nvrhi::DrawArguments().setVertexCount(3));

    commandList_->setTextureState(gBufferPosition_, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList_->setTextureState(gBufferNormal_, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList_->setTextureState(gBufferAlbedo_, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList_->setTextureState(gBufferMaterial_, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList_->commitBarriers();

    nvrhi::GraphicsState pbrLightingState;
    pbrLightingState.setPipeline(pbrLightingPipeline_);
    pbrLightingState.setFramebuffer(framebuffer);
    pbrLightingState.setViewport(fullscreenViewport);
    pbrLightingState.addBindingSet(pbrLightingBindingSet_);

    commandList_->setResourceStatesForBindingSet(pbrLightingBindingSet_);
    commandList_->commitBarriers();
    commandList_->setGraphicsState(pbrLightingState);
    commandList_->draw(nvrhi::DrawArguments().setVertexCount(3));

    imguiLayer_->render(commandList_, framebuffer, swapchain_->extent().width, swapchain_->extent().height);

    commandList_->setTextureState(target, nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
    commandList_->commitBarriers();
    commandList_->endMarker();
    commandList_->close();

    return device_->nvrhiDevice()->executeCommandList(commandList_);
}

void VulkanRenderer::shutdown()
{
    if (device_ && device_->nvrhiDevice()) {
        device_->nvrhiDevice()->waitForIdle();
    }

    releaseGpuResources();
    primitives_.clear();
    vertices_.clear();
    indices_.clear();
    swapchain_ = nullptr;
    device_ = nullptr;
    meshDirty_ = true;
}

void VulkanRenderer::releaseGpuResources()
{
    vertices_.clear();
    indices_.clear();
    framebuffers_.clear();
    imguiLayer_.reset();
    skyAtmosphereBindingSet_ = nullptr;
    pbrLightingBindingSet_ = nullptr;
    skyAtmosphereBindingLayout_ = nullptr;
    pbrLightingBindingLayout_ = nullptr;
    geometryBindingLayout_ = nullptr;
    lightingSampler_ = nullptr;
    gBufferFramebuffer_ = nullptr;
    gBufferMaterial_ = nullptr;
    gBufferAlbedo_ = nullptr;
    gBufferNormal_ = nullptr;
    gBufferPosition_ = nullptr;
    depthTexture_ = nullptr;
    lightingConstantsBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    vertexBuffer_ = nullptr;
    commandList_ = nullptr;
    skyAtmospherePipeline_ = nullptr;
    pbrLightingPipeline_ = nullptr;
    geometryPipeline_ = nullptr;
    inputLayout_ = nullptr;
    skyAtmosphereFragmentShader_ = nullptr;
    skyAtmosphereVertexShader_ = nullptr;
    pbrLightingFragmentShader_ = nullptr;
    pbrLightingVertexShader_ = nullptr;
    geometryFragmentShader_ = nullptr;
    geometryVertexShader_ = nullptr;
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    uploadedIndexCount_ = 0;
}

void VulkanRenderer::addPrimitive(BasicPrimitiveType type)
{
    const size_t index = primitives_.size();
    const float column = static_cast<float>(index % 5);
    const float row = static_cast<float>(index / 5);

    PrimitiveInstance primitive {};
    primitive.type = type;
    primitive.position[0] = -2.4f + column * 1.2f;
    primitive.position[1] = 0.0f;
    primitive.position[2] = -row * 1.2f;
    primitive.size = 0.75f;

    const std::array<std::array<float, 3>, 7> palette {{
        {{ 1.0f, 0.25f, 0.2f }},
        {{ 0.2f, 0.85f, 0.45f }},
        {{ 0.25f, 0.45f, 1.0f }},
        {{ 1.0f, 0.8f, 0.25f }},
        {{ 0.85f, 0.35f, 1.0f }},
        {{ 0.2f, 0.9f, 0.9f }},
        {{ 0.95f, 0.95f, 0.9f }},
    }};
    const auto& color = palette[index % palette.size()];
    primitive.color[0] = color[0];
    primitive.color[1] = color[1];
    primitive.color[2] = color[2];

    primitives_.push_back(primitive);
    meshDirty_ = true;
    gpuMeshDirty_ = true;
}

void VulkanRenderer::clearPrimitives()
{
    primitives_.clear();
    meshDirty_ = true;
    gpuMeshDirty_ = true;
    uploadedIndexCount_ = 0;
}

void VulkanRenderer::setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    primitives_ = primitives;
    meshDirty_ = true;
    gpuMeshDirty_ = true;
}

void VulkanRenderer::createFramebuffers(VulkanSwapchain& swapchain)
{
    createGBufferTextures(swapchain);
    createDepthTexture(swapchain);

    nvrhi::FramebufferDesc gBufferDesc;
    gBufferDesc.addColorAttachment(gBufferPosition_);
    gBufferDesc.addColorAttachment(gBufferNormal_);
    gBufferDesc.addColorAttachment(gBufferAlbedo_);
    gBufferDesc.addColorAttachment(gBufferMaterial_);
    gBufferDesc.setDepthAttachment(depthTexture_);
    gBufferFramebuffer_ = device_->nvrhiDevice()->createFramebuffer(gBufferDesc);
    if (!gBufferFramebuffer_) {
        throw std::runtime_error("Failed to create Vulkan renderer G-buffer framebuffer");
    }

    framebuffers_.reserve(swapchain.nvrhiImages().size());

    for (nvrhi::ITexture* image : swapchain.nvrhiImages()) {
        nvrhi::FramebufferDesc desc;
        desc.addColorAttachment(image);
        framebuffers_.push_back(device_->nvrhiDevice()->createFramebuffer(desc));
    }
}

void VulkanRenderer::createGBufferTextures(VulkanSwapchain& swapchain)
{
    auto createGBufferTexture = [&](const char* name, nvrhi::Format format) {
        return device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(swapchain.extent().width)
            .setHeight(swapchain.extent().height)
            .setFormat(format)
            .setIsRenderTarget(true)
            .setDebugName(name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget));
    };

    gBufferPosition_ = createGBufferTexture("GBufferPosition", nvrhi::Format::RGBA16_FLOAT);
    gBufferNormal_ = createGBufferTexture("GBufferNormal", nvrhi::Format::RGBA16_FLOAT);
    gBufferAlbedo_ = createGBufferTexture("GBufferAlbedo", nvrhi::Format::RGBA16_FLOAT);
    gBufferMaterial_ = createGBufferTexture("GBufferMaterial", nvrhi::Format::RGBA16_FLOAT);

    if (!gBufferPosition_ || !gBufferNormal_ || !gBufferAlbedo_ || !gBufferMaterial_) {
        throw std::runtime_error("Failed to create Vulkan renderer G-buffer textures");
    }
}

void VulkanRenderer::createDepthTexture(VulkanSwapchain& swapchain)
{
    depthTexture_ = device_->nvrhiDevice()->createTexture(nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(swapchain.extent().width)
        .setHeight(swapchain.extent().height)
        .setFormat(nvrhi::Format::D32)
        .setIsRenderTarget(true)
        .setDebugName("VulkanRendererDepth")
        .setInitialState(nvrhi::ResourceStates::DepthWrite)
        .setKeepInitialState(true));

    if (!depthTexture_) {
        throw std::runtime_error("Failed to create Vulkan renderer depth texture");
    }
}

void VulkanRenderer::createShaders()
{
    const std::string shaderDir = MENGINE_SHADER_BINARY_DIR;
    const std::vector<uint8_t> geometryVertexSpirV = VulkanShader::readSpirV(shaderDir + "/Primitive.vert.spv");
    const std::vector<uint8_t> geometryFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/Primitive.frag.spv");
    const std::vector<uint8_t> pbrLightingVertexSpirV = VulkanShader::readSpirV(shaderDir + "/PBRLighting.vert.spv");
    const std::vector<uint8_t> pbrLightingFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/PBRLighting.frag.spv");
    const std::vector<uint8_t> skyAtmosphereVertexSpirV = VulkanShader::readSpirV(shaderDir + "/SkyAtmosphere.vert.spv");
    const std::vector<uint8_t> skyAtmosphereFragmentSpirV = VulkanShader::readSpirV(shaderDir + "/SkyAtmosphere.frag.spv");

    geometryVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("GBufferVS")
            .setEntryName("main"),
        geometryVertexSpirV.data(),
        geometryVertexSpirV.size());

    geometryFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("GBufferPS")
            .setEntryName("main"),
        geometryFragmentSpirV.data(),
        geometryFragmentSpirV.size());

    pbrLightingVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("PBRLightingVS")
            .setEntryName("main"),
        pbrLightingVertexSpirV.data(),
        pbrLightingVertexSpirV.size());

    pbrLightingFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("PBRLightingPS")
            .setEntryName("main"),
        pbrLightingFragmentSpirV.data(),
        pbrLightingFragmentSpirV.size());

    skyAtmosphereVertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Vertex)
            .setDebugName("SkyAtmosphereVS")
            .setEntryName("main"),
        skyAtmosphereVertexSpirV.data(),
        skyAtmosphereVertexSpirV.size());

    skyAtmosphereFragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName("SkyAtmospherePS")
            .setEntryName("main"),
        skyAtmosphereFragmentSpirV.data(),
        skyAtmosphereFragmentSpirV.size());

    if (!geometryVertexShader_ || !geometryFragmentShader_ || !pbrLightingVertexShader_ ||
        !pbrLightingFragmentShader_ || !skyAtmosphereVertexShader_ || !skyAtmosphereFragmentShader_) {
        throw std::runtime_error("Failed to create NVRHI shaders for Vulkan renderer");
    }

    nvrhi::VertexAttributeDesc inputElements[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, position)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, normal)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, color)).setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, faceUv)).setElementStride(sizeof(Vertex)),
    };
    inputLayout_ = device_->nvrhiDevice()->createInputLayout(inputElements, 4, geometryVertexShader_);

    geometryBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PushConstants))));

    pbrLightingBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0).setConstantBufferOffset(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
        .addItem(nvrhi::BindingLayoutItem::Sampler(4))
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(5)));

    skyAtmosphereBindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setConstantBufferOffset(0))
        .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0)));
}

void VulkanRenderer::createPipeline()
{
    if (!gBufferFramebuffer_ || framebuffers_.empty()) {
        throw std::runtime_error("Cannot create Vulkan renderer pipeline without framebuffers");
    }

    nvrhi::RenderState renderState;
    renderState.rasterState.setCullNone();
    renderState.depthStencilState.enableDepthTest();
    renderState.depthStencilState.enableDepthWrite();
    renderState.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);

    nvrhi::GraphicsPipelineDesc geometryPipelineDesc;
    geometryPipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setInputLayout(inputLayout_)
        .setVertexShader(geometryVertexShader_)
        .setFragmentShader(geometryFragmentShader_)
        .setRenderState(renderState)
        .addBindingLayout(geometryBindingLayout_);

    geometryPipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        geometryPipelineDesc,
        gBufferFramebuffer_->getFramebufferInfo());

    if (!geometryPipeline_) {
        throw std::runtime_error("Failed to create NVRHI G-buffer pipeline for Vulkan renderer");
    }

    nvrhi::RenderState lightingRenderState;
    lightingRenderState.rasterState.setCullNone();
    lightingRenderState.depthStencilState.disableDepthTest();
    lightingRenderState.depthStencilState.disableDepthWrite();

    nvrhi::GraphicsPipelineDesc pbrLightingPipelineDesc;
    pbrLightingPipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setVertexShader(pbrLightingVertexShader_)
        .setFragmentShader(pbrLightingFragmentShader_)
        .setRenderState(lightingRenderState)
        .addBindingLayout(pbrLightingBindingLayout_);

    pbrLightingPipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        pbrLightingPipelineDesc,
        framebuffers_.front()->getFramebufferInfo());

    if (!pbrLightingPipeline_) {
        throw std::runtime_error("Failed to create NVRHI PBR lighting pipeline for Vulkan renderer");
    }

    nvrhi::GraphicsPipelineDesc skyAtmospherePipelineDesc;
    skyAtmospherePipelineDesc
        .setPrimType(nvrhi::PrimitiveType::TriangleList)
        .setVertexShader(skyAtmosphereVertexShader_)
        .setFragmentShader(skyAtmosphereFragmentShader_)
        .setRenderState(lightingRenderState)
        .addBindingLayout(skyAtmosphereBindingLayout_);

    skyAtmospherePipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        skyAtmospherePipelineDesc,
        framebuffers_.front()->getFramebufferInfo());

    if (!skyAtmospherePipeline_) {
        throw std::runtime_error("Failed to create NVRHI sky atmosphere pipeline for Vulkan renderer");
    }
}

void VulkanRenderer::createBuffers()
{
    vertexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxPrimitiveVertices * sizeof(Vertex))
        .setIsVertexBuffer(true)
        .setDebugName("PrimitiveVertexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));

    indexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(MaxPrimitiveIndices * sizeof(uint32_t))
        .setIsIndexBuffer(true)
        .setDebugName("PrimitiveIndexBuffer")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));

    lightingConstantsBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
        .setByteSize(sizeof(LightingConstants))
        .setIsConstantBuffer(true)
        .setDebugName("LightingConstants")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer));

    lightingSampler_ = device_->nvrhiDevice()->createSampler(nvrhi::SamplerDesc()
        .setAllFilters(false)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

    pbrLightingBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, gBufferPosition_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, gBufferNormal_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, gBufferAlbedo_))
        .addItem(nvrhi::BindingSetItem::Texture_SRV(3, gBufferMaterial_))
        .addItem(nvrhi::BindingSetItem::Sampler(4, lightingSampler_))
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(5, lightingConstantsBuffer_)),
        pbrLightingBindingLayout_);

    skyAtmosphereBindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, lightingConstantsBuffer_)),
        skyAtmosphereBindingLayout_);

    if (!vertexBuffer_ || !indexBuffer_ || !lightingConstantsBuffer_ || !lightingSampler_ ||
        !pbrLightingBindingSet_ || !skyAtmosphereBindingSet_) {
        throw std::runtime_error("Failed to create Vulkan renderer geometry buffers");
    }
}

void VulkanRenderer::rebuildMesh()
{
    vertices_.clear();
    indices_.clear();
    uploadedIndexCount_ = 0;
    vertices_.reserve((std::min)(static_cast<size_t>(MaxPrimitiveVertices), primitives_.size() * 12));
    indices_.reserve((std::min)(static_cast<size_t>(MaxPrimitiveIndices), primitives_.size() * 18));

    for (const PrimitiveInstance& primitive : primitives_) {
        switch (primitive.type) {
        case PrimitiveType::Triangle:
            appendTriangle(primitive);
            break;
        case PrimitiveType::Quad:
            appendQuad(primitive);
            break;
        case PrimitiveType::Cube:
            appendCube(primitive);
            break;
        case PrimitiveType::Sphere:
            appendSphere(primitive);
            break;
        }
    }
}

VulkanRenderer::PushConstants VulkanRenderer::buildPushConstants() const
{
    const float width = static_cast<float>(swapchain_->extent().width);
    const float height = static_cast<float>(swapchain_->extent().height);
    const float aspect = width / (std::max)(height, 1.0f);
    const Mat4 projection = perspective(camera_.fovDegrees * Pi / 180.0f, aspect, camera_.nearPlane, camera_.farPlane);
    const Mat4 view = lookAt(
        { camera_.position[0], camera_.position[1], camera_.position[2] },
        { camera_.target[0], camera_.target[1], camera_.target[2] },
        { 0.0f, 1.0f, 0.0f });
    const Mat4 viewProjection = multiply(projection, view);

    PushConstants constants {};
    std::copy(std::begin(viewProjection.m), std::end(viewProjection.m), std::begin(constants.viewProjection));
    constants.materialParameters[0] = materialMetallic_;
    constants.materialParameters[1] = materialRoughness_;
    constants.materialParameters[2] = materialAmbient_;
    constants.materialParameters[3] = 0.0f;
    return constants;
}

VulkanRenderer::LightingConstants VulkanRenderer::buildLightingConstants() const
{
    const float width = static_cast<float>(swapchain_->extent().width);
    const float height = static_cast<float>(swapchain_->extent().height);
    const float aspect = width / (std::max)(height, 1.0f);
    const float tanHalfFov = std::tan(camera_.fovDegrees * Pi / 360.0f);

    const Vec3 cameraPosition { camera_.position[0], camera_.position[1], camera_.position[2] };
    const Vec3 cameraTarget { camera_.target[0], camera_.target[1], camera_.target[2] };
    const Vec3 forward = normalize(subtract(cameraTarget, cameraPosition));
    const Vec3 right = normalize(cross(forward, { 0.0f, 1.0f, 0.0f }));
    const Vec3 up = cross(right, forward);
    const Vec3 sunDirection = normalize({ sunDirection_[0], sunDirection_[1], sunDirection_[2] });

    LightingConstants constants {};
    constants.cameraPosition[0] = cameraPosition.x;
    constants.cameraPosition[1] = cameraPosition.y;
    constants.cameraPosition[2] = cameraPosition.z;
    constants.cameraPosition[3] = 1.0f;
    constants.cameraForward[0] = forward.x;
    constants.cameraForward[1] = forward.y;
    constants.cameraForward[2] = forward.z;
    constants.cameraForward[3] = 0.0f;
    constants.cameraRight[0] = right.x;
    constants.cameraRight[1] = right.y;
    constants.cameraRight[2] = right.z;
    constants.cameraRight[3] = 0.0f;
    constants.cameraUp[0] = up.x;
    constants.cameraUp[1] = up.y;
    constants.cameraUp[2] = up.z;
    constants.cameraUp[3] = 0.0f;
    constants.sunDirection[0] = sunDirection.x;
    constants.sunDirection[1] = sunDirection.y;
    constants.sunDirection[2] = sunDirection.z;
    constants.sunDirection[3] = 0.0f;
    constants.sunColorAndIntensity[0] = sunColor_[0];
    constants.sunColorAndIntensity[1] = sunColor_[1];
    constants.sunColorAndIntensity[2] = sunColor_[2];
    constants.sunColorAndIntensity[3] = sunIntensity_;
    constants.pointLightCount[0] = static_cast<float>(std::clamp(activePointLightCount_, 0, 4));
    constants.pointLightCount[1] = 0.0f;
    constants.pointLightCount[2] = 0.0f;
    constants.pointLightCount[3] = 0.0f;

    for (int i = 0; i < 4; ++i) {
        constants.pointLightPosition[i][0] = pointLights_[i].position[0];
        constants.pointLightPosition[i][1] = pointLights_[i].position[1];
        constants.pointLightPosition[i][2] = pointLights_[i].position[2];
        constants.pointLightPosition[i][3] = 1.0f;
        constants.pointLightColorAndIntensity[i][0] = pointLights_[i].color[0];
        constants.pointLightColorAndIntensity[i][1] = pointLights_[i].color[1];
        constants.pointLightColorAndIntensity[i][2] = pointLights_[i].color[2];
        constants.pointLightColorAndIntensity[i][3] = pointLights_[i].intensity;
    }

    constants.skyParameters[0] = skyRayleighScale_;
    constants.skyParameters[1] = skyMieScale_;
    constants.skyParameters[2] = skyExposure_;
    constants.skyParameters[3] = 0.0f;
    constants.cloudParameters[0] = cloudCoverage_;
    constants.cloudParameters[1] = cloudDensity_;
    constants.cloudParameters[2] = cloudHeight_;
    constants.cloudParameters[3] = cloudThickness_;
    constants.cameraParameters[0] = tanHalfFov;
    constants.cameraParameters[1] = aspect;
    constants.cameraParameters[2] = width;
    constants.cameraParameters[3] = height;
    return constants;
}

void VulkanRenderer::drawPrimitivePanel()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Vulkan Renderer");

    if (ImGui::Button("Add Triangle")) {
        addPrimitive(PrimitiveType::Triangle);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Quad")) {
        addPrimitive(PrimitiveType::Quad);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Cube")) {
        addPrimitive(PrimitiveType::Cube);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Sphere")) {
        addPrimitive(PrimitiveType::Sphere);
    }

    if (ImGui::Button("Clear")) {
        clearPrimitives();
    }

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::SliderFloat3("Position", camera_.position, -10.0f, 10.0f);
    ImGui::SliderFloat("FOV", &camera_.fovDegrees, 20.0f, 100.0f);

    ImGui::Separator();
    ImGui::Text("Sun & Sky");
    ImGui::SliderFloat3("Sun Direction", sunDirection_, -1.0f, 1.0f);
    ImGui::ColorEdit3("Sun Color", sunColor_);
    ImGui::SliderFloat("Sun Intensity", &sunIntensity_, 0.0f, 10.0f);
    ImGui::SliderFloat("Rayleigh Scale", &skyRayleighScale_, 0.0f, 4.0f);
    ImGui::SliderFloat("Mie Scale", &skyMieScale_, 0.0f, 2.0f);
    ImGui::SliderFloat("Sky Exposure", &skyExposure_, 0.1f, 4.0f);
    ImGui::SliderFloat("Cloud Coverage", &cloudCoverage_, 0.0f, 1.0f);
    ImGui::SliderFloat("Cloud Density", &cloudDensity_, 0.0f, 2.0f);
    ImGui::SliderFloat("Cloud Height", &cloudHeight_, 8.0f, 80.0f);
    ImGui::SliderFloat("Cloud Thickness", &cloudThickness_, 1.0f, 30.0f);

    ImGui::Separator();
    ImGui::Text("Point Lights");
    ImGui::SliderInt("Active Lights", &activePointLightCount_, 0, 4);
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        if (ImGui::TreeNode("Point Light")) {
            ImGui::SliderFloat3("Position", pointLights_[i].position, -8.0f, 8.0f);
            ImGui::ColorEdit3("Color", pointLights_[i].color);
            ImGui::SliderFloat("Intensity", &pointLights_[i].intensity, 0.0f, 100.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("PBR Material");
    ImGui::SliderFloat("Metallic", &materialMetallic_, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &materialRoughness_, 0.04f, 1.0f);
    ImGui::SliderFloat("Ambient", &materialAmbient_, 0.0f, 0.2f);

    ImGui::Separator();
    ImGui::Text("Primitive count: %zu", primitives_.size());
    constexpr size_t MaxVisiblePrimitiveRows = 64;
    const size_t visibleRows = (std::min)(primitives_.size(), MaxVisiblePrimitiveRows);
    for (size_t index = 0; index < visibleRows; ++index) {
        ImGui::Text("%zu. %s", index + 1, primitiveName(primitives_[index].type));
    }
    if (primitives_.size() > visibleRows) {
        ImGui::Text("... %zu more hidden", primitives_.size() - visibleRows);
    }

    ImGui::End();
}

void VulkanRenderer::appendTriangle(const PrimitiveInstance& primitive)
{
    const uint32_t base = static_cast<uint32_t>(vertices_.size());
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    vertices_.push_back({ { cx, cy + half, cz }, { 0.0f, 0.9f, 0.2f }, { r, g, b }, { 0.5f, 1.0f } });
    vertices_.push_back({ { cx - half, cy - half, cz + half }, { -0.6f, -0.45f, 0.6f }, { r, g, b }, { 0.0f, 0.0f } });
    vertices_.push_back({ { cx + half, cy - half, cz + half }, { 0.6f, -0.45f, 0.6f }, { r, g, b }, { 1.0f, 0.0f } });
    vertices_.push_back({ { cx, cy - half, cz - half }, { 0.0f, -0.45f, -0.9f }, { r, g, b }, { 0.5f, 0.0f } });
    indices_.insert(indices_.end(), {
        base, base + 1, base + 2,
        base, base + 2, base + 3,
        base, base + 3, base + 1,
        base + 1, base + 3, base + 2,
    });
}

void VulkanRenderer::appendQuad(const PrimitiveInstance& primitive)
{
    const float hx = primitive.size * 0.62f;
    const float hy = primitive.size * 0.28f;
    const float hz = primitive.size * 0.42f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    const std::array<std::array<float, 3>, 8> corners {{
        {{ cx - hx, cy - hy, cz - hz }},
        {{ cx + hx, cy - hy, cz - hz }},
        {{ cx + hx, cy + hy, cz - hz }},
        {{ cx - hx, cy + hy, cz - hz }},
        {{ cx - hx, cy - hy, cz + hz }},
        {{ cx + hx, cy - hy, cz + hz }},
        {{ cx + hx, cy + hy, cz + hz }},
        {{ cx - hx, cy + hy, cz + hz }},
    }};

    auto appendFace = [&](std::array<uint32_t, 4> face, std::array<float, 3> normal) {
        const uint32_t faceBase = static_cast<uint32_t>(vertices_.size());
        const std::array<std::array<float, 2>, 4> uvs {{
            {{ 0.0f, 0.0f }},
            {{ 1.0f, 0.0f }},
            {{ 1.0f, 1.0f }},
            {{ 0.0f, 1.0f }},
        }};
        size_t uvIndex = 0;
        for (uint32_t cornerIndex : face) {
            const auto& position = corners[cornerIndex];
            const auto& uv = uvs[uvIndex++];
            vertices_.push_back({
                { position[0], position[1], position[2] },
                { normal[0], normal[1], normal[2] },
                { r, g, b },
                { uv[0], uv[1] }
            });
        }
        indices_.insert(indices_.end(), { faceBase, faceBase + 2, faceBase + 1, faceBase, faceBase + 3, faceBase + 2 });
    };

    if ((primitive.visibleFaces & PrimitiveFaceNegativeZ) != 0) {
        appendFace({ 0, 1, 2, 3 }, { 0.0f, 0.0f, -1.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveZ) != 0) {
        appendFace({ 4, 7, 6, 5 }, { 0.0f, 0.0f, 1.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeY) != 0) {
        appendFace({ 0, 4, 5, 1 }, { 0.0f, -1.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveY) != 0) {
        appendFace({ 3, 2, 6, 7 }, { 0.0f, 1.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFacePositiveX) != 0) {
        appendFace({ 1, 5, 6, 2 }, { 1.0f, 0.0f, 0.0f });
    }
    if ((primitive.visibleFaces & PrimitiveFaceNegativeX) != 0) {
        appendFace({ 0, 3, 7, 4 }, { -1.0f, 0.0f, 0.0f });
    }
}

void VulkanRenderer::appendCube(const PrimitiveInstance& primitive)
{
    const float half = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

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

    auto appendFace = [&](std::array<uint32_t, 4> face, std::array<float, 3> normal) {
        const uint32_t faceBase = static_cast<uint32_t>(vertices_.size());
        const std::array<std::array<float, 2>, 4> uvs {{
            {{ 0.0f, 0.0f }},
            {{ 1.0f, 0.0f }},
            {{ 1.0f, 1.0f }},
            {{ 0.0f, 1.0f }},
        }};
        size_t uvIndex = 0;
        for (uint32_t cornerIndex : face) {
            const auto& position = corners[cornerIndex];
            const auto& uv = uvs[uvIndex++];
            vertices_.push_back({
                { position[0], position[1], position[2] },
                { normal[0], normal[1], normal[2] },
                { r, g, b },
                { uv[0], uv[1] }
            });
        }
        indices_.insert(indices_.end(), { faceBase, faceBase + 2, faceBase + 1, faceBase, faceBase + 3, faceBase + 2 });
    };

    appendFace({ 0, 1, 2, 3 }, { 0.0f, 0.0f, -1.0f });
    appendFace({ 4, 7, 6, 5 }, { 0.0f, 0.0f, 1.0f });
    appendFace({ 0, 4, 5, 1 }, { 0.0f, -1.0f, 0.0f });
    appendFace({ 3, 2, 6, 7 }, { 0.0f, 1.0f, 0.0f });
    appendFace({ 1, 5, 6, 2 }, { 1.0f, 0.0f, 0.0f });
    appendFace({ 0, 3, 7, 4 }, { -1.0f, 0.0f, 0.0f });
}

void VulkanRenderer::appendSphere(const PrimitiveInstance& primitive)
{
    constexpr uint32_t rings = 16;
    constexpr uint32_t segments = 32;
    const uint32_t base = static_cast<uint32_t>(vertices_.size());
    const float radius = primitive.size * 0.5f;
    const float cx = primitive.position[0];
    const float cy = primitive.position[1];
    const float cz = primitive.position[2];
    const float r = primitive.color[0];
    const float g = primitive.color[1];
    const float b = primitive.color[2];

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float theta = v * Pi;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (uint32_t segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float phi = u * Pi * 2.0f;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            const float nx = sinTheta * cosPhi;
            const float ny = cosTheta;
            const float nz = sinTheta * sinPhi;

            vertices_.push_back({
                { cx + nx * radius, cy + ny * radius, cz + nz * radius },
                { nx, ny, nz },
                { r, g, b },
                { u, v }
            });
        }
    }

    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            const uint32_t a = base + ring * (segments + 1) + segment;
            const uint32_t b0 = a + segments + 1;
            indices_.insert(indices_.end(), {
                a, b0, a + 1,
                a + 1, b0, b0 + 1,
            });
        }
    }
}

} // namespace MEngine::RenderBackend::Vulkan
