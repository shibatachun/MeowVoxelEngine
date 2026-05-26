#include "CommandLineOptions.hpp"
#include "SandboxApplication.hpp"

#include "MEngine/Core/Log.hpp"
#include "MEngine/MEngine.hpp"
#include "MEngine/Windows/Window.hpp"

#include <chrono>
#include <exception>

int main(int argc, char** argv)
{
    try {
        const SandBox::CommandLineOptions options = SandBox::parseCommandLineOptions(argc, argv);

        MEngine::Core::initializeLogging();
        MENGINE_INFO("[SandBox] Requested graphics API: {}", SandBox::graphicsApiName(options.graphicsApi));
        if (options.enableRayTracing) {
            MENGINE_INFO("[SandBox] Ray tracing mode enabled by --rt");
        }

        MEngine::Windows::Window window({
            "MeowEngine SandBox",
            1280,
            720,
            true,
            options.graphicsApi == MEngine::GraphicsApi::Vulkan,
        });
        window.create();

        MEngine::Engine engine({
            "SandBox",
            window.width(),
            window.height(),
            options.graphicsApi,
            window.nativeHandle(),
            options.enableRayTracing,
        });

        engine.initialize();
        SandBox::SandboxApplication application(options);
        application.initialize(engine);

        auto lastFrameTime = std::chrono::steady_clock::now();
        while (window.isOpen() && engine.isRunning()) {
            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<float> delta = now - lastFrameTime;
            lastFrameTime = now;

            window.pollEvents();
            application.tick(engine, delta.count());
            engine.tick(delta.count());
        }

        engine.shutdown();
        window.destroy();
    } catch (const std::exception& exception) {
        MENGINE_ERROR("[SandBox] {}", exception.what());
        return 1;
    }

    return 0;
}
