#pragma once

#include <nvrhi/nvrhi.h>
#include <SDL3/SDL_events.h>

#include <chrono>
#include <cstdint>
#include <functional>

struct SDL_Window;

namespace MEngine::RenderBackend::Vulkan {

class VulkanDevice;
class VulkanSwapchain;

class VulkanImGuiLayer {
public:
    VulkanImGuiLayer() = default;
    ~VulkanImGuiLayer();

    VulkanImGuiLayer(const VulkanImGuiLayer&) = delete;
    VulkanImGuiLayer& operator=(const VulkanImGuiLayer&) = delete;

    void initialize(VulkanDevice& device, VulkanSwapchain& swapchain, nvrhi::IFramebuffer* framebuffer, SDL_Window* window);
    void render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, uint32_t width, uint32_t height);
    void shutdown();
    void setPanelCallback(std::function<void()> callback);
    void setStatsVisible(bool visible) { statsVisible_ = visible; }

private:
    void createFontTexture();
    void createShadersAndPipeline(nvrhi::IFramebuffer* framebuffer);
    void ensureBuffers(int vertexCount, int indexCount);
    void handleSdlEvent(const SDL_Event& event);

    static bool SDLCALL sdlEventWatch(void* userdata, SDL_Event* event);

    VulkanDevice* device_ = nullptr;
    SDL_Window* window_ = nullptr;
    uint32_t windowId_ = 0;
    bool eventWatchInstalled_ = false;
    bool statsVisible_ = true;
    nvrhi::TextureHandle fontTexture_;
    nvrhi::SamplerHandle fontSampler_;
    nvrhi::BindingLayoutHandle bindingLayout_;
    nvrhi::BindingSetHandle bindingSet_;
    nvrhi::ShaderHandle vertexShader_;
    nvrhi::ShaderHandle fragmentShader_;
    nvrhi::InputLayoutHandle inputLayout_;
    nvrhi::GraphicsPipelineHandle pipeline_;
    nvrhi::BufferHandle vertexBuffer_;
    nvrhi::BufferHandle indexBuffer_;
    int vertexBufferCapacity_ = 0;
    int indexBufferCapacity_ = 0;
    std::chrono::steady_clock::time_point lastFrameTime_ {};
    std::function<void()> panelCallback_;
};

} // namespace MEngine::RenderBackend::Vulkan
