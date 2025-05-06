#include "VulkanDevice.hpp"

VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice logicalDevice)
    : m_physicalDevice(physicalDevice), m_logicalDevice(logicalDevice) {
    // Constructor simply stores the provided handles.
    // No Vulkan objects are created or destroyed here.
}

VulkanDevice::~VulkanDevice() {
    // Destructor is empty as this class does not own the Vulkan handles.
    // Cleanup of VkDevice and VkPhysicalDevice is handled externally.
}

VkPhysicalDevice VulkanDevice::getPhysicalDevice() const {
    return m_physicalDevice;
}

VkDevice VulkanDevice::getLogicalDevice() const {
    return m_logicalDevice;
}