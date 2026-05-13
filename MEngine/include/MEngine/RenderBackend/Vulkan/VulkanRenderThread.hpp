#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace MEngine::RenderBackend::Vulkan {

class VulkanRenderThread {
public:
    VulkanRenderThread() = default;
    ~VulkanRenderThread();

    VulkanRenderThread(const VulkanRenderThread&) = delete;
    VulkanRenderThread& operator=(const VulkanRenderThread&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool running() const;

private:
    void run();

    std::atomic<bool> running_ = false;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};

} // namespace MEngine::RenderBackend::Vulkan
