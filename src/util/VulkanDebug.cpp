#include "VulkanDebug.hpp"
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <cstring>   // For strcmp

// Helper wrappers to resolve the debug utils extension functions at runtime.
// These use vkGetInstanceProcAddr (which volk provides once you've called volkLoadInstance(instance)).
static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func) return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) func(instance, debugMessenger, pAllocator);
}

// Define static members
const std::vector<const char*> VulkanDebug::validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebug::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    // Decide which messages to log (e.g., ignore INFO)
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
         std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE; // Should always return VK_FALSE
}

VulkanDebug::VulkanDebug() = default;

VulkanDebug::~VulkanDebug() {
    destroyMessenger(); // Ensure cleanup
}

void VulkanDebug::setupMessenger(VkInstance instance) {
    if (!enableValidationLayers) return;

    m_instance = instance; // Store for cleanup

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    // Use wrapper that resolves the extension function via vkGetInstanceProcAddr (or Volk).
    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
    std::cout << "Vulkan debug messenger set up successfully." << std::endl;
}

void VulkanDebug::destroyMessenger() {
    if (enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        // Use wrapper that resolves the extension function via vkGetInstanceProcAddr (or Volk).
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
        // m_instance = VK_NULL_HANDLE; // Keep m_instance as it might be needed if destroy is called multiple times, though not ideal.
                                     // Or clear it to signify it's destroyed. Let's clear it.
        m_instance = VK_NULL_HANDLE;
    }
}

bool VulkanDebug::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            std::cerr << "Validation layer not found: " << layerName << std::endl;
            return false;
        }
    }
    return true;
}

void VulkanDebug::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;
}