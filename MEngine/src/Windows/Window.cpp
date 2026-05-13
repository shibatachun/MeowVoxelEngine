#include "MEngine/Windows/Window.hpp"

#include "MEngine/Core/Log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <utility>

namespace MEngine::Windows {

class Window::Impl {
public:
    explicit Impl(WindowConfig windowConfig)
        : config(std::move(windowConfig)) {}

    WindowConfig config;
    SDL_Window* window = nullptr;
    bool ownsSdl = false;
    bool ownsVulkanLibrary = false;
    bool open = false;
};

Window::Window(WindowConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Window::~Window()
{
    destroy();
}

Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

void Window::create()
{
    if (impl_->window) {
        return;
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        impl_->ownsSdl = true;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (impl_->config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (impl_->config.enableVulkan) {
        if (!SDL_Vulkan_LoadLibrary(nullptr)) {
            throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError());
        }
        impl_->ownsVulkanLibrary = true;
        flags |= SDL_WINDOW_VULKAN;
    }

    impl_->window = SDL_CreateWindow(
        impl_->config.title.c_str(),
        impl_->config.width,
        impl_->config.height,
        flags);

    if (!impl_->window) {
        if (impl_->ownsVulkanLibrary) {
            SDL_Vulkan_UnloadLibrary();
            impl_->ownsVulkanLibrary = false;
        }
        if (impl_->ownsSdl) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            impl_->ownsSdl = false;
        }
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    impl_->open = true;
    MENGINE_INFO("[Windows] SDL3 window created: {} {}x{}{}",
        impl_->config.title,
        impl_->config.width,
        impl_->config.height,
        impl_->config.enableVulkan ? " Vulkan" : "");
}

void Window::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            impl_->open = false;
        }
    }
}

void Window::destroy()
{
    if (impl_->window) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
        impl_->open = false;
        MENGINE_INFO("[Windows] SDL3 window destroyed");
    }

    if (impl_->ownsVulkanLibrary) {
        SDL_Vulkan_UnloadLibrary();
        impl_->ownsVulkanLibrary = false;
    }

    if (impl_->ownsSdl) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        impl_->ownsSdl = false;
    }
}

bool Window::isOpen() const
{
    return impl_->open;
}

int Window::width() const
{
    return impl_->config.width;
}

int Window::height() const
{
    return impl_->config.height;
}

void* Window::nativeHandle() const
{
    return impl_->window;
}

} // namespace MEngine::Windows
