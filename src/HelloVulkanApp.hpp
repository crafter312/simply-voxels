#ifndef HELLO_VULKAN_APP_HPP
#define HELLO_VULKAN_APP_HPP

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <stdexcept> // Include for std::runtime_error if needed in header, though likely only needed in .cpp

// Forward declaration for the debug messenger handle if needed,
// but it's private so maybe not necessary here.
// struct VkDebugUtilsMessengerEXT_T;
// using VkDebugUtilsMessengerEXT = VkDebugUtilsMessengerEXT_T*;

class HelloVulkanApp {
public:
    void run();

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    void initWindow();
    void initVulkan();
    void createInstance();
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    void setupDebugMessenger();
    void mainLoop();
    void cleanup();
};

#endif // HELLO_VULKAN_APP_HPP