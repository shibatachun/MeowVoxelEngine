#include "MEngine/RenderBackend/Vulkan/VulkanRenderThread.hpp"

#include "MEngine/Core/Log.hpp"

namespace MEngine::RenderBackend::Vulkan {

VulkanRenderThread::~VulkanRenderThread()
{
    stop();
}

void VulkanRenderThread::start()
{
    if (running_) {
        return;
    }

    running_ = true;
    worker_ = std::thread(&VulkanRenderThread::run, this);
    MENGINE_INFO("[RenderBackend] Vulkan render thread started");
}

void VulkanRenderThread::stop()
{
    if (!running_) {
        return;
    }

    running_ = false;
    condition_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    MENGINE_INFO("[RenderBackend] Vulkan render thread stopped");
}

bool VulkanRenderThread::running() const
{
    return running_;
}

void VulkanRenderThread::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        condition_.wait(lock);
    }
}

} // namespace MEngine::RenderBackend::Vulkan
