#include "VulkanDevice.hpp"
#include "../VulkanDebug.hpp" // For VulkanDebug::enableValidationLayers and validationLayers
#include "VulkanSwapChain.hpp" // For SwapChainSupportDetails and querySupport

#include <stdexcept>
#include <iostream>
#include <set>
#include <cstring> // For strcmp

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions, VulkanDebug& vulkanDebug) {
    pickPhysicalDevice(instance, surface); // Pass surface for queue family and swap chain checks
    createLogicalDevice(instance, requiredDeviceExtensions, vulkanDebug); // Pass instance for older layer compatibility if needed

    // Get queue handles
    vkGetDeviceQueue(m_logicalDevice, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_logicalDevice, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
    std::cout << "VulkanDevice initialized: Physical and Logical devices created, queues retrieved." << std::endl;
}

VulkanDevice::~VulkanDevice() {
    if (m_logicalDevice != VK_NULL_HANDLE) {
        vkDestroyDevice(m_logicalDevice, nullptr);
        m_logicalDevice = VK_NULL_HANDLE;
        std::cout << "Logical device destroyed." << std::endl;
    }
}

VkPhysicalDevice VulkanDevice::getPhysicalDevice() const {
    return m_physicalDevice;
}
VkDevice VulkanDevice::getLogicalDevice() const {
    return m_logicalDevice;
}

const QueueFamilyIndices& VulkanDevice::getQueueFamilyIndices() const {
    return m_queueFamilyIndices;
}

VkQueue VulkanDevice::getGraphicsQueue() const {
    return m_graphicsQueue;
}

VkQueue VulkanDevice::getPresentQueue() const {
    return m_presentQueue;
}

const VkPhysicalDeviceFeatures& VulkanDevice::getEnabledFeatures() const {
    return m_enabledFeatures;
}

void VulkanDevice::pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Temporary: Use the same deviceExtensions as HelloVulkanApp used to pass.
    // This should ideally be passed in or be a member if it varies.
    // For now, assuming VK_KHR_SWAPCHAIN_EXTENSION_NAME is the key one for suitability.
    const std::vector<const char*> tempRequiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    for (const auto& device : devices) {
        if (isDeviceSuitable(device, surface, tempRequiredExtensions)) {
            m_physicalDevice = device;
            // Store the queue families found for the suitable device
            m_queueFamilyIndices = findQueueFamiliesInternal(device, surface);
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    std::cout << "Selected Physical Device: " << properties.deviceName << std::endl;

    // After selecting the physical device, query its features and decide which ones to enable.
    VkPhysicalDeviceFeatures deviceSupportedFeatures;
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &deviceSupportedFeatures);
    m_enabledFeatures = {}; // Clear any previous
    if (deviceSupportedFeatures.samplerAnisotropy) {
        m_enabledFeatures.samplerAnisotropy = VK_TRUE;
        std::cout << "Sampler Anisotropy feature is supported and will be enabled for the logical device." << std::endl;
    } else {
        std::cout << "Sampler Anisotropy feature is NOT supported by the selected physical device." << std::endl;
    }

    // Enable fillModeNonSolid if supported (required for wireframe rendering)
    if (deviceSupportedFeatures.fillModeNonSolid) {
        m_enabledFeatures.fillModeNonSolid = VK_TRUE;
    } else {
        // fillModeNonSolid is required for wireframe rendering.
        throw std::runtime_error("Selected physical device does not support fillModeNonSolid, which is required for wireframe rendering.");
    }
}

void VulkanDevice::createLogicalDevice(VkInstance instance, const std::vector<const char*>& requiredDeviceExtensions, VulkanDebug& vulkanDebug) {
    // m_queueFamilyIndices should already be populated by pickPhysicalDevice
    if (!m_queueFamilyIndices.isComplete()) {
        throw std::runtime_error("Queue families not found before creating logical device. Physical device picking likely failed.");
    }

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &m_enabledFeatures; // Use the features determined during physical device picking
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredDeviceExtensions.data();

    if (VulkanDebug::enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(VulkanDebug::validationLayers.size());
        createInfo.ppEnabledLayerNames = VulkanDebug::validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions) const {
    QueueFamilyIndices indices = findQueueFamiliesInternal(device, surface);
    bool extensionsSupported = checkDeviceExtensionSupportInternal(device, requiredDeviceExtensions);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails support = VulkanSwapChain::querySupport(device, surface);
        swapChainAdequate = !support.formats.empty() && !support.presentModes.empty();
    }

    // VkPhysicalDeviceFeatures supportedFeatures; // We check for specific features like samplerAnisotropy during pickPhysicalDevice
    // vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    // For general suitability, queue families, extensions, and swap chain are key.
    // Specific features are enabled if available but might not make a device "unsuitable" if missing, depending on app requirements.
    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

QueueFamilyIndices VulkanDevice::findQueueFamiliesInternal(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) {
            break;
        }
        i++;
    }
    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupportInternal(VkPhysicalDevice device, const std::vector<const char*>& requiredDeviceExtensions) const {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensionsSet(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensionsSet.erase(extension.extensionName);
    }
    return requiredExtensionsSet.empty();
}