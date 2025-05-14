#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <string> // For std::string in error messages, though not strictly required by VulkanDevice itself

// Forward declare VulkanDebug for constructor parameter
class VulkanDebug;

// Structure to hold queue family indices
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

/**
 * @class VulkanDevice
 * @brief A simple wrapper class to hold Vulkan physical and logical device handles.
 *
 * This class does not own the Vulkan handles; it merely provides a convenient
 * way to pass them around and access them, particularly for other manager classes
 * like VulkanDescriptorSetManager that need access to the VkDevice.
 */
class VulkanDevice {
public:
    // Constructor now takes instance, surface, and debug info to select and create devices
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions, VulkanDebug& vulkanDebug);
    ~VulkanDevice();

    // Getters
    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getLogicalDevice() const;
    const QueueFamilyIndices& getQueueFamilyIndices() const;
    VkQueue getGraphicsQueue() const;
    VkQueue getPresentQueue() const;
    const VkPhysicalDeviceFeatures& getEnabledFeatures() const;

private:
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_logicalDevice = VK_NULL_HANDLE;
    QueueFamilyIndices m_queueFamilyIndices;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures m_enabledFeatures{};

    // --- Device Selection and Creation ---
    void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void createLogicalDevice(VkInstance instance, const std::vector<const char*>& requiredDeviceExtensions, VulkanDebug& vulkanDebug);

    // --- Helper Methods for Device Suitability ---
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions) const;
    QueueFamilyIndices findQueueFamiliesInternal(VkPhysicalDevice device, VkSurfaceKHR surface) const; // Renamed to avoid conflict if used elsewhere
    bool checkDeviceExtensionSupportInternal(VkPhysicalDevice device, const std::vector<const char*>& requiredDeviceExtensions) const; // Renamed

    // Store required extensions if needed for multiple checks, or pass them.
    // For now, passing them to isDeviceSuitable and createLogicalDevice.
};

// Note: SwapChainSupportDetails is used internally by isDeviceSuitable via VulkanSwapChain::querySupport,
// so it doesn't need to be a member or forward-declared here unless exposed publicly.