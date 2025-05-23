#include "VulkanSwapChain.hpp"

#include <limits>
#include <algorithm>
#include <stdexcept>
#include <array>
#include <iostream> // For messages

VulkanSwapChain::VulkanSwapChain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, GLFWwindow* window, const QueueFamilyIndices& queueIndices)
    : instanceRef(instance),
      physicalDeviceRef(physicalDevice),
      deviceRef(device),
      surfaceRef(surface),
      windowRef(window),
      m_queueIndices(queueIndices) // Store a copy of the queue indices
{
    if (instanceRef == VK_NULL_HANDLE || physicalDeviceRef == VK_NULL_HANDLE || deviceRef == VK_NULL_HANDLE || surfaceRef == VK_NULL_HANDLE || windowRef == nullptr || !m_queueIndices.isComplete()) {
        throw std::runtime_error("VulkanSwapChain received null or invalid handles during construction!");
    }
}

VulkanSwapChain::~VulkanSwapChain() {
    cleanup(); // Ensure cleanup happens on destruction
}

void VulkanSwapChain::init() {
    createSwapChainInternal();
    createImageViews();
    // Framebuffers are created separately via createFramebuffers()
}

void VulkanSwapChain::cleanup() {
    std::cout << "Cleaning up swap chain resources..." << std::endl;
    // Destroy framebuffers first
    for (auto framebuffer : swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(deviceRef, framebuffer, nullptr);
        }
    }
    swapChainFramebuffers.clear();

    // Destroy image views
    for (auto imageView : swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(deviceRef, imageView, nullptr);
        }
    }
    swapChainImageViews.clear();

    // Destroy swap chain
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(deviceRef, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }
    std::cout << "Swap chain resource cleanup finished." << std::endl;
}

void VulkanSwapChain::cleanupForRecreation() {
    // Only clean up objects that need to be recreated
    for (auto framebuffer : swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(deviceRef, framebuffer, nullptr);
        }
    }
    swapChainFramebuffers.clear();

    for (auto imageView : swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(deviceRef, imageView, nullptr);
        }
    }
    swapChainImageViews.clear();

    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(deviceRef, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }
}

void VulkanSwapChain::recreate(VkRenderPass renderPass, VkImageView depthImageView) {
    cleanupForRecreation();
    createSwapChainInternal();
    createImageViews();
    createFramebuffers(renderPass, depthImageView); // Recreate framebuffers with the new views/extent and depth view
}

void VulkanSwapChain::createSwapChainInternal() {
    SwapChainSupportDetails support = querySupport(physicalDeviceRef, surfaceRef);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surfaceRef;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndicesArray[] = {m_queueIndices.graphicsFamily.value(), m_queueIndices.presentFamily.value()};

    if (m_queueIndices.graphicsFamily != m_queueIndices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesArray;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE; // oldSwapchain is handled externally if needed

    if (vkCreateSwapchainKHR(deviceRef, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(deviceRef, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(deviceRef, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void VulkanSwapChain::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(deviceRef, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views!");
        }
    }
}

void VulkanSwapChain::createFramebuffers(VkRenderPass renderPass, VkImageView depthImageView) {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { swapChainImageViews[i], depthImageView }; // Include depth image view

        // VkImageView attachments[] = { swapChainImageViews[i] };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass; // Use the provided render pass
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size()); // Use the size of the attachments array
        framebufferInfo.pAttachments = attachments.data(); // Get a pointer to the underlying data
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(deviceRef, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

VkResult VulkanSwapChain::acquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(deviceRef, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, imageIndex);
}

VkImageView VulkanSwapChain::getImageView(uint32_t index) const {
    if (index >= swapChainImageViews.size()) {
        throw std::out_of_range("Invalid swap chain image view index requested");
    }
    return swapChainImageViews[index];
}

VkFramebuffer VulkanSwapChain::getFramebuffer(uint32_t index) const {
    if (index >= swapChainFramebuffers.size()) {
        throw std::out_of_range("Invalid swap chain framebuffer index requested");
    }
    return swapChainFramebuffers[index];
}

// --- Static Helper Implementations ---

SwapChainSupportDetails VulkanSwapChain::querySupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR VulkanSwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) { // Removed const from parameter
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0]; // Fallback to the first available format
}

VkPresentModeKHR VulkanSwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            std::cout << "Swap Chain: Using Present Mode Mailbox" << std::endl;
            return availablePresentMode;
        }
    }
    std::cout << "Swap Chain: Using Present Mode FIFO" << std::endl;
    return VK_PRESENT_MODE_FIFO_KHR; // Guaranteed to be available
}

VkExtent2D VulkanSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        // Need to query window size if extent is variable
        int width, height;
        // Assuming windowRef is valid and points to the GLFW window
        // This part requires access to the window handle, passed during construction
        glfwGetFramebufferSize(windowRef, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}