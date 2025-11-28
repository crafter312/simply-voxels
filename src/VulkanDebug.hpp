#ifndef VULKAN_DEBUG_HPP
#define VULKAN_DEBUG_HPP

#include "render/VulkanCommon.hpp" // Centralized header for Volk and GLFW

#include <vector>
#include <string>

class VulkanDebug {
public:
    // Configuration: Enable validation layers in debug builds?
    #ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
    #else
    static constexpr bool enableValidationLayers = true;
    #endif

    // List of validation layers to enable
    static const std::vector<const char*> validationLayers;

    VulkanDebug();
    ~VulkanDebug();

    // Call this after instance creation
    void setupMessenger(VkInstance instance);
    // Call this before instance destruction (or rely on destructor)
    void destroyMessenger();

    // Checks if all requested validation layers are available
    static bool checkValidationLayerSupport();

    // Populates the VkDebugUtilsMessengerCreateInfoEXT struct
    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

private:
    VkInstance m_instance = VK_NULL_HANDLE; // Store instance for cleanup
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    // The debug callback function
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

#endif // VULKAN_DEBUG_HPP