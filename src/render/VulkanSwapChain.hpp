#ifndef VULKAN_SWAP_CHAIN_HPP
#define VULKAN_SWAP_CHAIN_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

// Structure to hold queue family indices
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Structure to hold swap chain support details
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanSwapChain {
public:
    VulkanSwapChain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, GLFWwindow* window, std::shared_ptr<QueueFamilyIndices> queueIndices);
    ~VulkanSwapChain();

    // Initialize swap chain, image views. Call createFramebuffers separately after render pass exists.
    void init();
    // Create framebuffers (needs render pass)
    void createFramebuffers(VkRenderPass renderPass, VkImageView depthImageView); // Added depthImageView parameter
    // Cleanup all resources
    void cleanup();
    // Cleanup resources needed for recreation
    void cleanupForRecreation();
    // Recreate swap chain (e.g., after window resize)
    void recreate(VkRenderPass renderPass, VkImageView depthImageView); // Added depthImageView parameter

    // Acquire the next image index from the swap chain
    VkResult acquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t* imageIndex);

    // Public helpers needed by VulkanRenderer during recreation
    void createSwapChainInternal();
    void createImageViews();

    // Getters
    VkSwapchainKHR getSwapChainHandle() const { return swapChain; }
    VkFormat getFormat() const { return swapChainImageFormat; }
    VkExtent2D getExtent() const { return swapChainExtent; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }
    VkImageView getImageView(uint32_t index) const;
    VkFramebuffer getFramebuffer(uint32_t index) const;

    // Static helpers
    static SwapChainSupportDetails querySupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    // References to external objects
    VkInstance instanceRef;
    VkPhysicalDevice physicalDeviceRef;
    VkDevice deviceRef;
    VkSurfaceKHR surfaceRef;
    GLFWwindow* windowRef;
    std::shared_ptr<QueueFamilyIndices> queueIndicesRef; // Changed to shared_ptr

    // Swap Chain objects
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Private helpers
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};

#endif // VULKAN_SWAP_CHAIN_HPP