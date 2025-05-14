#include "HelloVulkanApp.hpp"
#include "render/VulkanRenderer.hpp" // Include the new renderer header
#include "render/VulkanDevice.hpp"   // Include the new VulkanDevice header
#include "InputManager.hpp"          // Include the InputManager header
#include "render/VulkanSwapChain.hpp" // Include for querySupport and SwapChainSupportDetails
#include "block/BlockRegistry.hpp"         // Include the BlockRegistry header
#include "block/Blocks.hpp"                // Include the new Blocks header
#include "Camera.hpp"                // Include the Camera header
#include "world/World.hpp"                 // Include the World header
#include "VulkanDebug.hpp"           // Include the new VulkanDebug header

#include <iostream>
#include <vector>
#include <stdexcept>
#include <optional>
#include <set>
#include <cstdint> // Necessary for UINT32_MAX
#include <limits> // Necessary for std::numeric_limits
#include <algorithm> // Necessary for std::clamp
#include <cstdlib>
#include <cstring> // Required for strcmp


// --- Constants and Configuration ---

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

// Note: enableValidationLayers and validationLayers are now part of VulkanDebug

const std::vector<const char*> REQUIRED_DEVICE_EXTENSIONS = { // Renamed for clarity
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
// Note: Debug messenger functions (CreateDebugUtilsMessengerEXT, DestroyDebugUtilsMessengerEXT, debugCallback)
// are now static private members of VulkanDebug.

// --- GLFW Framebuffer Resize Callback ---

void HelloVulkanApp::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<HelloVulkanApp*>(glfwGetWindowUserPointer(window));
    if (app->renderer) { // Ensure renderer exists before signaling
        app->renderer->framebufferResized = true;
    }
}

// --- HelloVulkanApp Method Implementations ---

HelloVulkanApp::HelloVulkanApp() {}

// Default destructor in implementation file to satisfy completeness requirement of 
// unique_ptr template type.
HelloVulkanApp::~HelloVulkanApp() = default;

void HelloVulkanApp::run() {
    std::cout << "Starting application..." << std::endl;
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void HelloVulkanApp::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Allow the window to be resized (GLFW_TRUE is the default, so we could also remove this line)
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Simple Vulkan Window", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window!");
    }
    std::cout << "GLFW window created successfully." << std::endl;

    // Store pointer to this instance for use in callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    // Capture and hide the cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // --- Initialize Input Manager ---
    inputManager = std::make_shared<InputManager>(window);
    std::cout << "InputManager initialized." << std::endl;
}

void HelloVulkanApp::initVulkan() {
    // --- Initialize Vulkan Debugging ---
    // Create VulkanDebug instance first, as it contains static configuration
    // like enableValidationLayers used by createInstance().
    vulkanDebug = std::make_unique<VulkanDebug>();

    createInstance();
    std::cout << "Vulkan Instance created." << std::endl;
    // Setup debug messenger after instance creation
    vulkanDebug->setupMessenger(instance); // This will print its own success message
     createSurface();
    std::cout << "Vulkan Surface created." << std::endl;

    // --- Create Vulkan Device (Physical & Logical) ---
    vulkanDevice = std::make_unique<VulkanDevice>(instance, surface, REQUIRED_DEVICE_EXTENSIONS, *vulkanDebug);
    // VulkanDevice constructor will print its own success messages for physical/logical device.

    // --- Create Camera ---
    camera = std::make_shared<Camera>(inputManager); // Pass the inputManager to the Camera constructor
    // Example: Set initial camera position or orientation if not done in constructor
    // camera->position = glm::vec3(0.0f, 0.0f, 5.0f);
    std::cout << "Camera created and initialized." << std::endl;

    // --- Create and Populate Block Registry ---
    blockRegistry = std::make_unique<BlockRegistry>();
    // Use the new centralized function to register block types
    Blocks::registerBlockTypes(*blockRegistry);
    std::cout << "BlockRegistry created and populated." << std::endl;

    // --- Create World ---
    // Pass the camera to the World constructor
    world = std::make_unique<World>(camera);
    std::cout << "World created with initial blocks." << std::endl;

    // --- Create and Initialize Renderer ---
    renderer = std::make_unique<VulkanRenderer>(
        window,
        instance,
        surface,
        *vulkanDevice, // Pass the VulkanDevice object by reference
        camera
    );
    renderer->init(*blockRegistry, *world); // Pass BlockRegistry and World to init()
    // --- End Renderer Init ---

    std::cout << "Vulkan initialization complete." << std::endl;
}

void HelloVulkanApp::createInstance() {
    if (VulkanDebug::enableValidationLayers && !VulkanDebug::checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Simple Vulkan App";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (VulkanDebug::enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (VulkanDebug::enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(VulkanDebug::validationLayers.size());
        createInfo.ppEnabledLayerNames = VulkanDebug::validationLayers.data();
        VulkanDebug::populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void HelloVulkanApp::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

// --- Helper Implementations ---

void HelloVulkanApp::mainLoop() {
    lastFrameTime = static_cast<float>(glfwGetTime()); // Initialize lastFrameTime before loop

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Calculate delta time
        float currentTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;

        if (inputManager) {
            inputManager->update(); // Call input manager update
        }

        if (camera) {
            camera->update(deltaTime); // Call camera update (e.g., for smoothing, animations, or if it polls input itself)
        }

        if (renderer) {
            renderer->drawFrame(); // Call renderer's drawFrame
        }
    }

    // Wait for the logical device to finish operations before cleanup
    if (vulkanDevice && vulkanDevice->getLogicalDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkanDevice->getLogicalDevice());
    }
}

void HelloVulkanApp::cleanup() {
    std::cout << "Cleaning up..." << std::endl;

    // Renderer holds Vulkan objects that depend on the device, so destroy it first.
    // The renderer's destructor handles its internal cleanup.
    renderer.reset(); // Calls VulkanRenderer destructor

    // InputManager is managed by unique_ptr, will be cleaned up automatically
    inputManager.reset();

    // BlockRegistry is managed by unique_ptr, will be cleaned up automatically
    blockRegistry.reset();

    // World is managed by unique_ptr, will be cleaned up automatically
    world.reset();

    // VulkanDevice's destructor will handle destroying the logical device.
    vulkanDevice.reset();

    // vulkanDebug's destructor will handle destroying the debug messenger.
    vulkanDebug.reset();

    // Destroy surface
    if (surface != VK_NULL_HANDLE) { // Check handle before destroying
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    // Destroy instance
    if (instance != VK_NULL_HANDLE) { // Check handle before destroying
        vkDestroyInstance(instance, nullptr);
    }

    // Destroy window and terminate GLFW
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    std::cout << "Cleanup complete." << std::endl;
}