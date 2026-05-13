#include "MEngine/RenderBackend/Vulkan/VulkanRHI.hpp"

#include "MEngine/Camera/Camera.hpp"
#include "MEngine/Core/Log.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanCommandContext.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanDevice.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanRenderer.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanRenderThread.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanSwapchain.hpp"
#include "MEngine/RenderBackend/Vulkan/VulkanUtils.hpp"

#include <nvrhi/vulkan.h>
#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <stdexcept>

namespace MEngine::RenderBackend::Vulkan {

class VulkanRHI::Impl {
public:
    void initialize(SDL_Window* window, const char* applicationName)
    {
        if (!window) {
            throw std::runtime_error("Vulkan requires a valid SDL window handle");
        }

        window_ = window;
        device = std::make_unique<VulkanDevice>();
        device->initialize(window, applicationName);

        swapchain = std::make_unique<VulkanSwapchain>();
        swapchain->initialize(*device, window);

        commandContext = std::make_unique<VulkanCommandContext>();
        commandContext->initialize(*device);

        renderer = std::make_unique<VulkanRenderer>();
        renderer->initialize(*device, *swapchain, window_);

        renderThread = std::make_unique<VulkanRenderThread>();
        renderThread->start();

        MENGINE_INFO("[RenderBackend] Vulkan RHI initialized with {}", device->physicalDeviceName());
    }

    void shutdown()
    {
        if (renderThread) {
            renderThread->stop();
            renderThread.reset();
        }

        renderer.reset();
        commandContext.reset();
        swapchain.reset();
        device.reset();
    }

    void beginFrame()
    {
        frameHasImage = false;
    }

    void endFrame(const Camera::CameraState* camera)
    {
        if (!device || !swapchain || !commandContext || !renderer) {
            return;
        }

        if (camera) {
            renderer->setCameraState(*camera);
        }

        const uint32_t frameIndex = commandContext->currentFrameIndex();
        waitForFrameSlot(frameIndex);

        VkSemaphore imageAvailableSemaphore = commandContext->imageAvailableSemaphore(frameIndex);
        VkSemaphore renderFinishedSemaphore = commandContext->renderFinishedSemaphore(frameIndex);

        const VkResult acquireResult = vkAcquireNextImageKHR(
            device->device(),
            swapchain->swapchain(),
            UINT64_MAX,
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            MENGINE_INFO("[RenderBackend] Vulkan swapchain is out of date during acquire; recreating");
            recreateSwapchain();
            return;
        }

        checkVulkan(acquireResult, "vkAcquireNextImageKHR");

        nvrhiVulkanDevice()->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, imageAvailableSemaphore, 0);
        nvrhiVulkanDevice()->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, renderFinishedSemaphore, 0);
        lastGraphicsSubmissionId = renderer->render(imageIndex);
        frameSubmissionIds[frameIndex] = lastGraphicsSubmissionId;

        VkSwapchainKHR swapchainHandle = swapchain->swapchain();
        VkPresentInfoKHR presentInfo {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult presentResult = vkQueuePresentKHR(device->graphicsQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            MENGINE_INFO("[RenderBackend] Vulkan present requested swapchain recreation");
            recreateSwapchain();
        } else {
            checkVulkan(presentResult, "vkQueuePresentKHR");
        }

        ++garbageCollectionFrameCounter;
        if (garbageCollectionFrameCounter >= 60) {
            device->nvrhiDevice()->runGarbageCollection();
            garbageCollectionFrameCounter = 0;
        }
        commandContext->advanceFrame();
        frameHasImage = false;
    }

    void recreateSwapchain()
    {
        if (!device || !swapchain || !window_) {
            return;
        }

        device->nvrhiDevice()->waitForIdle();
        vkDeviceWaitIdle(device->device());

        commandContext.reset();

        swapchain->shutdown();
        swapchain->initialize(*device, window_);

        commandContext = std::make_unique<VulkanCommandContext>();
        commandContext->initialize(*device);

        renderer->recreateSwapchainResources(*swapchain, window_);
        imageIndex = 0;
        frameSubmissionIds.fill(0);
        frameHasImage = false;
        garbageCollectionFrameCounter = 0;
    }

    [[nodiscard]] nvrhi::vulkan::IDevice* nvrhiVulkanDevice() const
    {
        return static_cast<nvrhi::vulkan::IDevice*>(device->nvrhiDevice().Get());
    }

    void waitForFrameSlot(uint32_t frameIndex)
    {
        const uint64_t submissionId = frameSubmissionIds[frameIndex];
        if (submissionId == 0) {
            return;
        }

        const uint64_t completedId = nvrhiVulkanDevice()->queueGetCompletedInstance(nvrhi::CommandQueue::Graphics);
        if (completedId >= submissionId) {
            return;
        }

        VkSemaphore timelineSemaphore = nvrhiVulkanDevice()->getQueueSemaphore(nvrhi::CommandQueue::Graphics);
        VkSemaphoreWaitInfo waitInfo {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timelineSemaphore;
        waitInfo.pValues = &submissionId;
        checkVulkan(vkWaitSemaphores(device->device(), &waitInfo, UINT64_MAX), "vkWaitSemaphores");
    }

    void setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
    {
        if (renderer) {
            renderer->setPrimitiveInstances(primitives);
        }
    }

    [[nodiscard]] nvrhi::DeviceHandle nvrhiDevice() const
    {
        return device ? device->nvrhiDevice() : nullptr;
    }

    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanCommandContext> commandContext;
    std::unique_ptr<VulkanRenderer> renderer;
    std::unique_ptr<VulkanRenderThread> renderThread;
    SDL_Window* window_ = nullptr;
    uint32_t imageIndex = 0;
    uint32_t garbageCollectionFrameCounter = 0;
    std::array<uint64_t, VulkanCommandContext::FramesInFlight> frameSubmissionIds {};
    uint64_t lastGraphicsSubmissionId = 0;
    bool frameHasImage = false;
};

VulkanRHI::VulkanRHI()
    : impl_(std::make_unique<Impl>()) {}

VulkanRHI::~VulkanRHI() = default;

void VulkanRHI::initialize(void* nativeWindowHandle, const char* applicationName)
{
    impl_->initialize(static_cast<SDL_Window*>(nativeWindowHandle), applicationName);
}

void VulkanRHI::beginFrame()
{
    impl_->beginFrame();
}

void VulkanRHI::endFrame(const Camera::CameraState* camera)
{
    impl_->endFrame(camera);
}

void VulkanRHI::setPrimitiveInstances(const std::vector<PrimitiveInstance>& primitives)
{
    impl_->setPrimitiveInstances(primitives);
}

void VulkanRHI::shutdown()
{
    impl_->shutdown();
}

nvrhi::DeviceHandle VulkanRHI::device() const
{
    return impl_->nvrhiDevice();
}

} // namespace MEngine::RenderBackend::Vulkan
