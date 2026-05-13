#pragma once

#include <memory>
#include <string>

namespace MEngine::Windows {

struct WindowConfig {
    std::string title = "MeowEngine";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool enableVulkan = false;
};

class Window {
public:
    explicit Window(WindowConfig config = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    void create();
    void pollEvents();
    void destroy();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] void* nativeHandle() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MEngine::Windows
