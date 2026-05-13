#include "MEngine/RenderBackend/Vulkan/VulkanImGuiLayer.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanShader.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanSwapchain.hpp"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cfloat>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MENGINE_SHADER_BINARY_DIR
#define MENGINE_SHADER_BINARY_DIR "."
#endif

namespace MEngine::RenderBackend::Vulkan {

namespace {

struct ImGuiPushConstants {
    float scale[2];
    float translate[2];
};

nvrhi::Format indexFormat()
{
    return sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
}

} // namespace

VulkanImGuiLayer::~VulkanImGuiLayer()
{
    shutdown();
}

void VulkanImGuiLayer::initialize(VulkanDevice& device, VulkanSwapchain& swapchain, nvrhi::IFramebuffer* framebuffer, SDL_Window* window)
{
    device_ = &device;
    window_ = window;
    windowId_ = window ? SDL_GetWindowID(window) : 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(static_cast<float>(swapchain.extent().width), static_cast<float>(swapchain.extent().height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::StyleColorsDark();

    createFontTexture();
    createShadersAndPipeline(framebuffer);
    if (window_) {
        eventWatchInstalled_ = SDL_AddEventWatch(&VulkanImGuiLayer::sdlEventWatch, this);
    }
    lastFrameTime_ = std::chrono::steady_clock::now();

    MENGINE_INFO("[RenderBackend] ImGui initialized on NVRHI Vulkan backend");
}

void VulkanImGuiLayer::render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, uint32_t width, uint32_t height)
{
    ImGuiIO& io = ImGui::GetIO();
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - lastFrameTime_;
    lastFrameTime_ = now;

    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = (std::max)(delta.count(), 1.0f / 10000000.0f);

    ImGui::NewFrame();
    if (panelCallback_) {
        panelCallback_();
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("MeowEngine Stats", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);
    ImGui::Text("FPS %.1f", io.Framerate);
    ImGui::Text("Frame %.3f ms", 1000.0f / (std::max)(io.Framerate, 0.001f));
    ImGui::End();
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->TotalVtxCount == 0) {
        return;
    }

    ensureBuffers(drawData->TotalVtxCount, drawData->TotalIdxCount);

    size_t vertexOffsetBytes = 0;
    size_t indexOffsetBytes = 0;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        commandList->writeBuffer(vertexBuffer_, drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert), vertexOffsetBytes);
        commandList->writeBuffer(indexBuffer_, drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(ImDrawIdx), indexOffsetBytes);
        vertexOffsetBytes += drawList->VtxBuffer.Size * sizeof(ImDrawVert);
        indexOffsetBytes += drawList->IdxBuffer.Size * sizeof(ImDrawIdx);
    }

    const ImGuiPushConstants pushConstants {
        { 2.0f / drawData->DisplaySize.x, -2.0f / drawData->DisplaySize.y },
        { -1.0f - drawData->DisplayPos.x * 2.0f / drawData->DisplaySize.x,
          1.0f + drawData->DisplayPos.y * 2.0f / drawData->DisplaySize.y }
    };

    nvrhi::GraphicsState state;
    state.setPipeline(pipeline_);
    state.setFramebuffer(framebuffer);
    state.addBindingSet(bindingSet_);
    state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(vertexBuffer_).setSlot(0).setOffset(0));
    state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(indexBuffer_).setFormat(indexFormat()).setOffset(0));

    commandList->setResourceStatesForBindingSet(bindingSet_);
    commandList->setBufferState(vertexBuffer_, nvrhi::ResourceStates::VertexBuffer);
    commandList->setBufferState(indexBuffer_, nvrhi::ResourceStates::IndexBuffer);
    commandList->commitBarriers();
    commandList->setGraphicsState(state);
    commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

    int globalVertexOffset = 0;
    int globalIndexOffset = 0;
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        for (const ImDrawCmd& drawCommand : drawList->CmdBuffer) {
            if (drawCommand.UserCallback) {
                continue;
            }

            ImVec2 clipMin((drawCommand.ClipRect.x - clipOffset.x) * clipScale.x, (drawCommand.ClipRect.y - clipOffset.y) * clipScale.y);
            ImVec2 clipMax((drawCommand.ClipRect.z - clipOffset.x) * clipScale.x, (drawCommand.ClipRect.w - clipOffset.y) * clipScale.y);
            clipMin.x = (std::max)(clipMin.x, 0.0f);
            clipMin.y = (std::max)(clipMin.y, 0.0f);
            clipMax.x = (std::min)(clipMax.x, static_cast<float>(width));
            clipMax.y = (std::min)(clipMax.y, static_cast<float>(height));
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                continue;
            }

            nvrhi::ViewportState viewport;
            viewport.addViewport(nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height)));
            viewport.addScissorRect(nvrhi::Rect(
                static_cast<int>(clipMin.x),
                static_cast<int>(clipMax.x),
                static_cast<int>(clipMin.y),
                static_cast<int>(clipMax.y)));
            state.setViewport(viewport);
            commandList->setGraphicsState(state);
            commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

            commandList->drawIndexed(nvrhi::DrawArguments()
                .setVertexCount(drawCommand.ElemCount)
                .setStartIndexLocation(drawCommand.IdxOffset + globalIndexOffset)
                .setStartVertexLocation(drawCommand.VtxOffset + globalVertexOffset));
        }

        globalIndexOffset += drawList->IdxBuffer.Size;
        globalVertexOffset += drawList->VtxBuffer.Size;
    }
}

void VulkanImGuiLayer::shutdown()
{
    if (eventWatchInstalled_) {
        SDL_RemoveEventWatch(&VulkanImGuiLayer::sdlEventWatch, this);
        eventWatchInstalled_ = false;
    }

    if (device_ && device_->nvrhiDevice()) {
        device_->nvrhiDevice()->waitForIdle();
    }

    indexBuffer_ = nullptr;
    vertexBuffer_ = nullptr;
    pipeline_ = nullptr;
    inputLayout_ = nullptr;
    fragmentShader_ = nullptr;
    vertexShader_ = nullptr;
    bindingSet_ = nullptr;
    bindingLayout_ = nullptr;
    fontSampler_ = nullptr;
    fontTexture_ = nullptr;
    vertexBufferCapacity_ = 0;
    indexBufferCapacity_ = 0;
    windowId_ = 0;
    window_ = nullptr;
    device_ = nullptr;

    if (ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
    }
}

void VulkanImGuiLayer::setPanelCallback(std::function<void()> callback)
{
    panelCallback_ = std::move(callback);
}

void VulkanImGuiLayer::handleSdlEvent(const SDL_Event& event)
{
    if (!ImGui::GetCurrentContext()) {
        return;
    }

    auto eventWindowId = [](const SDL_Event& sdlEvent) -> uint32_t {
        switch (sdlEvent.type) {
        case SDL_EVENT_MOUSE_MOTION:
            return sdlEvent.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return sdlEvent.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return sdlEvent.wheel.windowID;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            return sdlEvent.window.windowID;
        default:
            return 0;
        }
    };

    const uint32_t targetWindowId = eventWindowId(event);
    if (targetWindowId != 0 && windowId_ != 0 && targetWindowId != windowId_) {
        return;
    }

    const bool altDown = (SDL_GetModState() & SDL_KMOD_ALT) != 0;

    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (!altDown) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        }
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!altDown) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
            break;
        }

        int button = -1;
        if (event.button.button == SDL_BUTTON_LEFT) {
            button = ImGuiMouseButton_Left;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            button = ImGuiMouseButton_Right;
        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
            button = ImGuiMouseButton_Middle;
        } else if (event.button.button == SDL_BUTTON_X1) {
            button = 3;
        } else if (event.button.button == SDL_BUTTON_X2) {
            button = 4;
        }

        if (button >= 0) {
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(event.button.x, event.button.y);
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!altDown) {
            break;
        }

        float wheelX = event.wheel.x;
        float wheelY = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            wheelX *= -1.0f;
            wheelY *= -1.0f;
        }

        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(event.wheel.mouse_x, event.wheel.mouse_y);
        io.AddMouseWheelEvent(wheelX, wheelY);
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        break;
    default:
        break;
    }
}

bool SDLCALL VulkanImGuiLayer::sdlEventWatch(void* userdata, SDL_Event* event)
{
    if (userdata && event) {
        static_cast<VulkanImGuiLayer*>(userdata)->handleSdlEvent(*event);
    }

    return true;
}

void VulkanImGuiLayer::createFontTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    auto desc = nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setWidth(static_cast<uint32_t>(width))
        .setHeight(static_cast<uint32_t>(height))
        .setFormat(nvrhi::Format::R8_UNORM)
        .setDebugName("ImGuiFontAtlas")
        .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);

    fontTexture_ = device_->nvrhiDevice()->createTexture(desc);
    fontSampler_ = device_->nvrhiDevice()->createSampler(nvrhi::SamplerDesc());

    nvrhi::CommandListHandle uploadCommandList = device_->nvrhiDevice()->createCommandList();
    uploadCommandList->open();
    uploadCommandList->writeTexture(fontTexture_, 0, 0, pixels, static_cast<size_t>(width));
    uploadCommandList->setPermanentTextureState(fontTexture_, nvrhi::ResourceStates::ShaderResource);
    uploadCommandList->close();
    device_->nvrhiDevice()->executeCommandList(uploadCommandList);
    device_->nvrhiDevice()->waitForIdle();
}

void VulkanImGuiLayer::createShadersAndPipeline(nvrhi::IFramebuffer* framebuffer)
{
    const std::string shaderDir = MENGINE_SHADER_BINARY_DIR;
    const std::vector<uint8_t> vertexSpirV = VulkanShader::readSpirV(shaderDir + "/ImGui.vert.spv");
    const std::vector<uint8_t> fragmentSpirV = VulkanShader::readSpirV(shaderDir + "/ImGui.frag.spv");

    vertexShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setDebugName("ImGuiVS").setEntryName("main"),
        vertexSpirV.data(),
        vertexSpirV.size());
    fragmentShader_ = device_->nvrhiDevice()->createShader(
        nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setDebugName("ImGuiPS").setEntryName("main"),
        fragmentSpirV.data(),
        fragmentSpirV.size());

    nvrhi::VertexAttributeDesc inputElements[] = {
        nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
        nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
    };
    inputLayout_ = device_->nvrhiDevice()->createInputLayout(inputElements, 3, vertexShader_);

    bindingLayout_ = device_->nvrhiDevice()->createBindingLayout(nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::AllGraphics)
        .setBindingOffsets(nvrhi::VulkanBindingOffsets().setSamplerOffset(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Sampler(1))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ImGuiPushConstants))));

    bindingSet_ = device_->nvrhiDevice()->createBindingSet(nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, fontTexture_))
        .addItem(nvrhi::BindingSetItem::Sampler(1, fontSampler_)),
        bindingLayout_);

    nvrhi::RenderState renderState;
    renderState.rasterState.setCullNone();
    renderState.depthStencilState.disableDepthTest();
    renderState.depthStencilState.disableDepthWrite();
    renderState.blendState.targets[0]
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setBlendOp(nvrhi::BlendOp::Add)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha)
        .setBlendOpAlpha(nvrhi::BlendOp::Add);

    pipeline_ = device_->nvrhiDevice()->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setPrimType(nvrhi::PrimitiveType::TriangleList)
            .setInputLayout(inputLayout_)
            .setVertexShader(vertexShader_)
            .setFragmentShader(fragmentShader_)
            .setRenderState(renderState)
            .addBindingLayout(bindingLayout_),
        framebuffer->getFramebufferInfo());
}

void VulkanImGuiLayer::ensureBuffers(int vertexCount, int indexCount)
{
    if (vertexBufferCapacity_ < vertexCount) {
        vertexBufferCapacity_ = vertexCount + 5000;
        vertexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
            .setByteSize(static_cast<uint64_t>(vertexBufferCapacity_) * sizeof(ImDrawVert))
            .setIsVertexBuffer(true)
            .setDebugName("ImGuiVertexBuffer")
            .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));
    }

    if (indexBufferCapacity_ < indexCount) {
        indexBufferCapacity_ = indexCount + 10000;
        indexBuffer_ = device_->nvrhiDevice()->createBuffer(nvrhi::BufferDesc()
            .setByteSize(static_cast<uint64_t>(indexBufferCapacity_) * sizeof(ImDrawIdx))
            .setIsIndexBuffer(true)
            .setDebugName("ImGuiIndexBuffer")
            .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));
    }
}

} // namespace MEngine::RenderBackend::Vulkan
