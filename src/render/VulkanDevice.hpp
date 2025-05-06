#pragma once

#include <vulkan/vulkan.h>

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
    // Constructor takes existing physical and logical device handles
    VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice logicalDevice);
    ~VulkanDevice();

    // Getters
    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getLogicalDevice() const;

private:
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_logicalDevice;
};