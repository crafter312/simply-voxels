#include "HelloVulkanApp.hpp"
#include "util/DebugLog.hpp"
#include "render/VulkanRenderer.hpp" // Include the new renderer header
#include "render/VulkanDevice.hpp"   // Include the new VulkanDevice header
#include "ui/InputManager.hpp"          // Include the InputManager header
#include "render/VulkanSwapChain.hpp" // Include for querySupport and SwapChainSupportDetails
#include "block/BlockRegistry.hpp"         // Include the BlockRegistry header
#include "block/Blocks.hpp"                // Include the new Blocks header
#include "Camera.hpp"                // Include the Camera header
#include "world/World.hpp"                 // Include the World header
#include "Player.hpp"                // Include the Player header
#include "VulkanDebug.hpp"           // Include the new VulkanDebug header

#include "ui/UIManager.hpp"          // Include the new UIManager header
#include "world/WorldMetadata.hpp"   // Include WorldMetadata for the new startGame function
#include "world/SaveGameManager.hpp" // Include the new SaveGameManager header
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
#include <chrono>


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
    VK_LOG("Starting application...");
    // --- Initialize Volk FIRST ---
    // This must be done before any other Vulkan or GLFW calls.
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize volk!");
    }

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
    VK_LOG("GLFW window created successfully.");

    // Store pointer to this instance for use in callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    // --- Initialize Input Manager ---
    inputManager = std::make_shared<InputManager>(window);
    if (!inputManager) throw std::runtime_error("Failed to create InputManager!");
    VK_LOG("InputManager initialized.");
}

void HelloVulkanApp::initVulkan() {
    // --- Initialize Vulkan Debugging ---
    // Create VulkanDebug instance first, as it contains static configuration
    // like enableValidationLayers used by createInstance().
    vulkanDebug = std::make_unique<VulkanDebug>();

    createInstance();
    VK_LOG("Vulkan Instance created.");
    // Setup debug messenger after instance creation
    vulkanDebug->setupMessenger(instance); // This will print its own success message
     createSurface();
    VK_LOG("Vulkan Surface created.");

    // --- Create Vulkan Device (Physical & Logical) ---
    vulkanDevice = std::make_unique<VulkanDevice>(instance, surface, REQUIRED_DEVICE_EXTENSIONS, *vulkanDebug);
    if (!vulkanDevice) throw std::runtime_error("Failed to create Vulkan Device!");
    // Ensure Volk loads all device-level function pointers for this logical device
    volkLoadDevice(vulkanDevice->getLogicalDevice());
    // VulkanDevice constructor will print its own success messages for physical/logical device.

    // --- Create Camera ---
    camera = std::make_shared<Camera>(inputManager); // Pass the inputManager to the Camera constructor
    if (!camera) throw std::runtime_error("Failed to create Camera!");
    VK_LOG("Camera created and initialized.");

    // --- Create and Populate Block Registry ---
    blockRegistry = std::make_unique<BlockRegistry>();
    // Use the new centralized function to register block types
    Blocks::registerBlockTypes(*blockRegistry);
    std::cout << "BlockRegistry created and populated." << std::endl;

    // --- Initialize Save Game Manager ---
    // This should be done early, as it scans for worlds on startup.
    m_saveGameManager = std::make_unique<SaveGameManager>("../run/saves/");
    if (!m_saveGameManager) throw std::runtime_error("Failed to create SaveGameManager!");

    // --- Create World ---
    // NOTE: This world creation will eventually be moved into the startGame method
    // and will take WorldMetadata as an argument. For now, we keep it to allow
    // the application to run without UI interaction.
    world = std::make_unique<World>(camera); 
    if (!world) throw std::runtime_error("Failed to create World!");
    VK_LOG("World created with initial blocks.");

    // --- Create Player ---
    player = std::make_unique<Player>(camera, *world, *blockRegistry, inputManager);
    if (!player) throw std::runtime_error("Failed to create Player!");
    VK_LOG("Player created.");

    // --- Create and Initialize Renderer ---
    renderer = std::make_unique<VulkanRenderer>(
        *window,       // Dereference window to pass as GLFWwindow&
        instance,
        surface,
        *vulkanDevice, // Pass the VulkanDevice object
        *world,        // Pass the World object by reference
        *blockRegistry, // Pass the BlockRegistry object by reference
        camera,
        *player        // Pass the Player object by reference
    );
    if (!renderer) throw std::runtime_error("Failed to create VulkanRenderer!");
    renderer->init(); // init no longer takes BlockRegistry
                                    // --- End Renderer Init ---

    std::cout << "Vulkan initialization complete." << std::endl;

    // Set initial state after all initialization
    m_currentState = GameState::STARTUP;
}
void HelloVulkanApp::resumeGame() {
    m_currentState = GameState::IN_GAME;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void HelloVulkanApp::quitToMenu() {
    m_currentState = GameState::MAIN_MENU;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // Unload the current world and clear GPU data to return to a clean "limbo" state.
    if (world) {
        world->unload(); // Assumes this method exists to save and clear world data.
    }
    renderer->clearWorldRenderDataAndReset();
}

void HelloVulkanApp::startGame(const WorldMetadata& worldMeta) {
    VK_LOG("Starting game for world: " << worldMeta.worldName);

    // 1. Change the game state
    m_currentState = GameState::LOADING;

    // 2. Disable the cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 3. Update the last played timestamp for the selected world
    m_saveGameManager->updateLastPlayed(worldMeta.directoryName);

    // 4. Load the world data using the provided metadata.
    // This assumes World::loadFromMetadata exists and will prepare the world for loading.
    world->loadFromMetadata(worldMeta);
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

    if (VulkanDebug::enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(VulkanDebug::validationLayers.size());
        createInfo.ppEnabledLayerNames = VulkanDebug::validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr; // Explicitly null
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    // Load instance-level functions from Volk AFTER the instance is created.
    volkLoadInstance(instance);
}

void HelloVulkanApp::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

// --- Helper Implementations ---

void HelloVulkanApp::mainLoop() {
    lastFrame = static_cast<float>(glfwGetTime()); // Initialize lastFrame before loop

    // Loop for single time game startup state
    while (!glfwWindowShouldClose(window)) {
        if (startup()) {
            break; // Exit loop once startup is complete
        }
    }

    // Main application loop
    while (!glfwWindowShouldClose(window)) {
        VK_LOG("[DBG] loop top");

        VK_LOG("[DBG] before glfwPollEvents");
        glfwPollEvents();
        VK_LOG("[DBG] after glfwPollEvents");

        // Calculate delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        VK_LOG("[DBG] before ImGui_NewFrame");
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        VK_LOG("[DBG] after ImGui_NewFrame");

        VK_LOG("[DBG] before inputManager->update");
        if (inputManager) {
            inputManager->update();
            VK_LOG("[DBG] inputManager updated");
        } else {
            std::cerr << "[DBG] inputManager IS NULL" << std::endl;
        }

        VK_LOG("[DBG] before update()");
        update();
        VK_LOG("[DBG] after update()");

        VK_LOG("[DBG] before render()");
        // Narrow down renderer usage safely
        if (!renderer) {
            std::cerr << "[DBG] renderer IS NULL" << std::endl;
        } else {
            VK_LOG("[DBG] renderer pointer: " << renderer.get() << "");
        }

        // Call render but guard drawFrame to see if that's the crash site.
        // You can temporarily comment out the drawFrame call to see if crash stops.
        // Note: leave calls that don't touch Vulkan active to test other subsystems.
        try {
            render();
            VK_LOG("[DBG] render() returned");
        } catch (const std::exception& e) {
            std::cerr << "[DBG] render() threw std::exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[DBG] render() threw unknown exception" << std::endl;
        }

        VK_LOG("[DBG] end of loop iteration");
    }

    // Wait for the logical device to finish operations before cleanup
    if (vulkanDevice && vulkanDevice->getLogicalDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkanDevice->getLogicalDevice());
    }
}

// Game startup dispatcher
bool HelloVulkanApp::startup() {
    // Currently, we just transition to the main menu immediately.
    // You could add splash screen logic or initial loading here.
    VK_LOG("State: STARTUP -> MAIN_MENU");

    // Create the UI Manager
    m_uiManager = std::make_unique<UIManager>(*this, *m_saveGameManager);

    m_currentState = GameState::MAIN_MENU;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Show cursor for menu
    return true; // Indicate that startup is complete
}

// Main update dispatcher
void HelloVulkanApp::update() {
    switch (m_currentState) {
        case GameState::MAIN_MENU:
            updateMainMenu();
            break;
        case GameState::LOADING:
            updateLoading(deltaTime);
            break;
        case GameState::IN_GAME:
            updateInGame(deltaTime);
            break;
        case GameState::PAUSED:
            updatePaused();
            break;
    }
}

// Main render dispatcher
void HelloVulkanApp::render() {
    switch (m_currentState) {
        case GameState::MAIN_MENU:
            renderMainMenu();
            break;
        case GameState::LOADING:
            renderLoading();
            break;
        case GameState::IN_GAME:
            renderInGame();
            break;
        case GameState::PAUSED:
            renderPaused();
            break;
    }

    // All render paths should call ImGui::Render() and renderer->drawFrame() to present the frame
    ImGui::Render();
    if (m_currentState == GameState::LOADING) {
        renderer->drawFrameUIOnly();
        return;
    }
    renderer->drawFrame();
}

void HelloVulkanApp::updateMainMenu() {
    // The logic for button clicks is now handled inside UIManager::drawMainMenu
    // and the renderMainMenu function. We leave this empty for now, but you
    // could add logic for things like menu animations here.
}

void HelloVulkanApp::renderMainMenu() {
    if (!m_uiManager) return;

    // Draw main menu, return early if no world selected
    std::optional<WorldMetadata> worldToLoad = m_uiManager->drawMainMenu();
    if (!worldToLoad.has_value()) return;

    VK_LOG("Starting game with new or existing world...");
    startGame(worldToLoad.value());
}

void HelloVulkanApp::updateLoading(float dt) {
    // This is our two-stage check for loading completion.
    // Stage 1: Wait for the world to finish its initial chunk generation pass.
    // Stage 2: Once generation is done, wait for the meshing pipeline to become fully idle.
    world->update(dt);
    if (world && world->isInitialChunkGenerationComplete() &&
        renderer && renderer->isMeshingPipelineIdle()) {
        VK_LOG("State: LOADING -> IN_GAME");
        m_currentState = GameState::IN_GAME;

        // Spawn player after world is fully loaded
        if (player && !player->hasSpawned()) {
            std::optional<glm::i64vec3> spawnPosOpt = world->getPlayerSpawnPos();
            if (spawnPosOpt) {
                glm::i64vec3 absoluteSpawnBlockPos = *spawnPosOpt; // This is the world block coord for player's feet

                std::optional<glm::ivec3> spawnChunkOpt = World::worldToChunkCoordinates(absoluteSpawnBlockPos);
                if (spawnChunkOpt) {
                    glm::ivec3 spawnChunk = *spawnChunkOpt;
                    std::optional<glm::ivec3> spawnLocalBlockOpt = World::worldToLocalCoordinates(absoluteSpawnBlockPos, spawnChunk);
                    if (spawnLocalBlockOpt) {
                        // Convert local block coords to vec3 for player's local position.
                        // The Y from getPlayerSpawnPos is already the feet level.
                        // Center the player on the XZ of the block.
                        // Add a small epsilon to Y to prevent clipping into the spawn block.
                        glm::vec3 spawnLocalPos = glm::vec3(*spawnLocalBlockOpt);
                        spawnLocalPos.x += 0.5f; // Center on X
                        spawnLocalPos.y += 0.001f; // Small epsilon for Y
                        spawnLocalPos.z += 0.5f; // Center on Z
                        player->setPosition(spawnChunk, spawnLocalPos);
                        VK_LOG("Player spawned at chunk: (" << spawnChunk.x << "," << spawnChunk.y << "," << spawnChunk.z << "), local: (" << spawnLocalPos.x << "," << spawnLocalPos.y << "," << spawnLocalPos.z << ")");
                    }
                }
            }
        }

        // Check that player has spawned successfully
        if (player && !player->hasSpawned())
            throw std::runtime_error("Player failed to spawn after loading world!");
    }
}

void HelloVulkanApp::renderLoading() {
    if (m_uiManager) {
        m_uiManager->drawLoadingScreen(); // bit of a misnomer, just defines the pause UI
    }
}

void HelloVulkanApp::updateInGame(float dt) {
    
    // Handle pause toggle first, as it might affect input processing for camera/player
    if (inputManager->isKeyPressed(KeyCode::Escape)) {
        VK_LOG("State: IN_GAME -> PAUSED");
        m_currentState = GameState::PAUSED;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Show cursor for pause menu
        world->update(dt);
        return; // Don't process any more game logic this frame
    }

    player->update(dt);
    camera->update(dt);
    world->update(dt);
}

void HelloVulkanApp::renderInGame() {
    // Draw the XYZ coordinate overlay if the UI manager and player exist.
    if (!m_uiManager || !player) return;
    
    m_uiManager->drawXYZCoordinateOverlay(player->getAbsoluteChunkPos(), player->getLocalPositionInChunk());
}

void HelloVulkanApp::updatePaused() {
    if (inputManager->isKeyPressed(KeyCode::Escape)) {
        VK_LOG("State: PAUSED -> IN_GAME");
        m_currentState = GameState::IN_GAME;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // Hide cursor for game
    }
}

void HelloVulkanApp::renderPaused() {
    if (!m_uiManager) return;
    m_uiManager->drawPauseMenu();

    // Also draw the coordinate overlay while paused.
    if (!player) return;
    m_uiManager->drawXYZCoordinateOverlay(player->getAbsoluteChunkPos(), player->getLocalPositionInChunk());
}

void HelloVulkanApp::cleanup() {
    VK_LOG("Cleaning up...");

    // Renderer holds Vulkan objects that depend on the device, so destroy it first.
    // The renderer's destructor handles its internal cleanup.
    renderer.reset(); // Calls VulkanRenderer destructor

    // UI Manager is managed by unique_ptr
    m_uiManager.reset();

    // SaveGameManager is managed by unique_ptr
    m_saveGameManager.reset();

    // Player is managed by unique_ptr, will be cleaned up automatically
    player.reset();

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