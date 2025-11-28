#include "VulkanRenderer.hpp"
#include "VulkanSwapChain.hpp" // Include the new swap chain class
#include "VulkanPipelineFactory.hpp" // Include the new pipeline factory class
#include "VulkanBufferManager.hpp" // Include the new buffer manager class
#include "VulkanDevice.hpp"      // Include the VulkanDevice wrapper class definition
// #include "../resource/VulkanTextureLoader.hpp" // ResourceManager handles this
#include "../resource/ModelLoader.hpp"         // For ModelLoader::loadGltfModel and ModelData
#include "../resource/ResourceManager.hpp"     // Include the ResourceManager
#include "../world/Chunk.hpp"                  // Include the Chunk class definition
#include "../world/World.hpp"                        // Include the World class definition
#include "../Camera.hpp"            // Include the Camera class definition
#include "../block/Blocks.hpp"      // Include the Blocks class for block IDs
#include "../block/Block.hpp"       // For Block definition
#include "../physics/VoxelShape.hpp"    // For VoxelShape
#include "../Player.hpp"            // Include the Player class definition
#include "../util/Raycaster.hpp" // For RaycastResult

#include <iostream>
#include <set>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <array>    // For descriptor pool sizes
#include <string>   // For shader file loading
#include <fstream>  // For shader file loading
#include <cstring> // For memcpy
#include <chrono>   // For std::chrono::seconds in future.wait_for

VulkanRenderer::VulkanRenderer(GLFWwindow& glfwWindow, VkInstance instance, VkSurfaceKHR surface, VulkanDevice& vulkanDevice, World& worldRef, BlockRegistry& blockRegistryRef, std::shared_ptr<Camera> cameraPtr, Player& playerRef)
    : window(glfwWindow), // Initialized with a reference
      instanceRef(instance),
      surfaceRef(surface),
      m_vulkanDeviceRef(vulkanDevice),
      m_blockRegistryRef(blockRegistryRef), // Initialize block registry reference
      m_world(worldRef), // Initialize world reference
      m_camera(cameraPtr),
      m_playerRef(playerRef) // Initialize player reference
{
    // The check for 'window' being null is removed as it's now a reference.
    // The caller is responsible for ensuring glfwWindow is valid.
    if (instanceRef == VK_NULL_HANDLE || surfaceRef == VK_NULL_HANDLE ||
        // Access device properties through m_vulkanDeviceRef
        m_vulkanDeviceRef.getPhysicalDevice() == VK_NULL_HANDLE || m_vulkanDeviceRef.getLogicalDevice() == VK_NULL_HANDLE ||
        !m_vulkanDeviceRef.getQueueFamilyIndices().isComplete() || m_vulkanDeviceRef.getGraphicsQueue() == VK_NULL_HANDLE || m_vulkanDeviceRef.getPresentQueue() == VK_NULL_HANDLE ||
        !m_camera) // Check if camera pointer is valid
    {
        throw std::runtime_error("VulkanRenderer received null or invalid handles during construction!");
    }
    // Create the swap chain manager. Since 'window' is now a GLFWwindow&,
    // we pass its address (&window) if VulkanSwapChain expects a GLFWwindow*.
    swapChainManager = std::make_unique<VulkanSwapChain>(instanceRef, m_vulkanDeviceRef.getPhysicalDevice(), m_vulkanDeviceRef.getLogicalDevice(), surfaceRef, &window, m_vulkanDeviceRef.getQueueFamilyIndices());

    // The line below caused the error and is no longer needed as we use m_vulkanDeviceRef
    // m_vulkanDeviceWrapper = std::make_unique<VulkanDevice>(physicalDeviceRef, deviceRef); 
    // Create the descriptor set manager
    descriptorSetManager = std::make_unique<VulkanDescriptorSetManager>();

    std::cout << "VulkanRenderer constructed." << std::endl;
}

VulkanRenderer::~VulkanRenderer() {
    std::cout << "Cleaning up VulkanRenderer..." << std::endl;

    // --- ImGui Cleanup ---
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    std::cout << "ImGui context and backends shut down." << std::endl;

    // m_threadManager's destructor will be called automatically, which safely stops and joins the worker thread.
    // Note: vkDeviceWaitIdle should be called before this destructor is invoked (e.g., in HelloVulkanApp::cleanup)

    // Destroy all per-chunk render data first. This will queue their Vulkan buffers for deletion.
    // This now just clears the map. The BufferManager's destructor handles the pool cleanup.
    m_chunkRenderData.clear();

    // Process any remaining items in deletion queues.
    // Assuming vkDeviceWaitIdle has been called externally or we'd call it here.
    std::cout << "VulkanRenderer Destructor: Processing final deletion queues..." << std::endl;
    for (size_t i = 0; i < m_deletionQueues.size(); ++i) {
        for (const auto& resource : m_deletionQueues[i]) {
            // At shutdown, we only care about destroying full buffers (like wireframe).
            // BufferRegions are part of the pools that the BufferManager will destroy entirely.
            // We do not need to free them individually here.
            if (resource.type == ResourceToDelete::Type::Buffer) {
                if (resource.bufferHandle != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.bufferHandle, nullptr);
                if (resource.memoryHandle != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memoryHandle, nullptr);
            }
        }
        m_deletionQueues[i].clear();
    }

    // Now that all sub-allocations are returned (or will be ignored as the pools are destroyed),
    // we can safely destroy the buffer manager.
    bufferManager.reset();
    // Descriptor set manager resources are cleaned up by its destructor
    descriptorSetManager.reset();
    resourceManager.reset(); // Clean up resource manager
    // m_vulkanDeviceWrapper is removed
    
    cleanupDepthResources(); // Clean up depth buffer resources

    swapChainManager.reset(); // Explicitly reset before other resources if needed, though RAII handles it

    // Destroy graphics pipeline and layout
    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vulkanDeviceRef.getLogicalDevice(), graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vulkanDeviceRef.getLogicalDevice(), pipelineLayout, nullptr);

    // Destroy render pass
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_vulkanDeviceRef.getLogicalDevice(), renderPass, nullptr);

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffersMapped[i] != nullptr) { // Check if mapped
            vkUnmapMemory(m_vulkanDeviceRef.getLogicalDevice(), uniformBuffersMemory[i]); // Unmap before freeing memory
            // uniformBuffersMapped[i] = nullptr; // Optional: vector will be cleared or resized
        }
        if (uniformBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), uniformBuffers[i], nullptr);
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), uniformBuffersMemory[i], nullptr);
    }

    // Destroy synchronization objects
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_vulkanDeviceRef.getLogicalDevice(), inFlightFences[i], nullptr);
    }

    // Clean up presentationFinishedSemaphores
    for (auto semaphore : presentationFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), semaphore, nullptr);
        }
    }

    imageAvailableSemaphores.clear();
    inFlightFences.clear();
    presentationFinishedSemaphores.clear();
    // Destroy command pool (also frees command buffers allocated from it)
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vulkanDeviceRef.getLogicalDevice(), commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    // Destroy wireframe buffers
    // The wireframe buffers are now sub-allocated from pools managed by VulkanBufferManager.
    // The manager's destructor is responsible for cleaning up the entire pool.
    // We no longer need to manually destroy these buffers here.

    if (m_wireframePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vulkanDeviceRef.getLogicalDevice(), m_wireframePipeline, nullptr);
    if (m_wireframePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vulkanDeviceRef.getLogicalDevice(), m_wireframePipelineLayout, nullptr);

    // Other resources (device, instance, surface) are managed by HelloVulkanApp
    std::cout << "VulkanRenderer cleanup complete." << std::endl;
}

void VulkanRenderer::init() {
    std::cout << "Initializing VulkanRenderer..." << std::endl;
    // Initialize swap chain (creates chain and image views)
    swapChainManager->init();
    std::cout << "Swap Chain initialized." << std::endl;
    
    createCommandPool(); // Create command pool early as ResourceManager might need it
    std::cout << "Command Pool created." << std::endl;

    // --- Initialize ResourceManager ---
    resourceManager = std::make_unique<ResourceManager>(m_vulkanDeviceRef.getPhysicalDevice(), m_vulkanDeviceRef.getLogicalDevice(), commandPool, m_vulkanDeviceRef.getGraphicsQueue());
    resourceManager->setDefaultModelPath("../resources/models/cube.glb"); // Default cube
    resourceManager->setDefaultTexturePath("../resources/textures/default_error.png"); // Default error texture
    resourceManager->loadAssetsFromRegistry(m_blockRegistryRef, true); // Load assets using the member BlockRegistry reference
    // Build the texture atlas.

    // --- Initialize ThreadManager ---
    m_threadManager = std::make_unique<VulkanThreadManager>(m_world, *resourceManager, m_blockRegistryRef);
    m_threadManager->start();

    resourceManager->buildTextureAtlas(m_blockRegistryRef);
    std::cout << "Assets loaded by ResourceManager." << std::endl;
    // Create buffer manager now that command pool and graphics queue exist
    // This needs to be created before depth resources and render pass if render pass depends on depth format
    bufferManager = std::make_unique<VulkanBufferManager>(m_vulkanDeviceRef.getLogicalDevice(), m_vulkanDeviceRef.getPhysicalDevice(), commandPool, m_vulkanDeviceRef.getGraphicsQueue());

    // Initialize presentationFinishedSemaphores (one per swap chain image)
    presentationFinishedSemaphores.resize(swapChainManager->getImageCount(), VK_NULL_HANDLE);

    // Initialize imagesInFlight fences (one per swap chain image)
    imagesInFlight.resize(swapChainManager->getImageCount(), VK_NULL_HANDLE);

    // Create depth buffer resources (needs swap chain extent, so after swapChainManager->init())
    bufferManager->createDepthResources(swapChainManager->getExtent(), depthImage, depthImageMemory, depthImageView, depthFormat);
    std::cout << "Depth Resources created." << std::endl;

    createRenderPass();
    std::cout << "Render Pass created." << std::endl;

    // Initialize the DescriptorSetManager first, as subsequent calls will need the device.
    descriptorSetManager->initialize(&m_vulkanDeviceRef, swapChainManager.get());
    std::cout << "DescriptorSetManager initialized." << std::endl;

    // --- Create ImGui Descriptor Pool ---
    descriptorSetManager->createImguiDescriptorPool();
    std::cout << "ImGui Descriptor Pool created by manager." << std::endl;

    // --- Initialize ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(&window, true);
    std::cout << "ImGui GLFW backend initialized." << std::endl;

    // Define descriptor set layout bindings (previously in createDescriptorSetLayout)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0; // Corresponds to "binding = 0" in the shader
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1; // We have one UBO
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // UBO is used in the vertex shader
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional, but good to be explicit
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1; // Next binding after UBO
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // Sampler is used in the fragment shader

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding, samplerLayoutBinding};
    descriptorSetManager->createDescriptorSetLayout(bindings);
    std::cout << "Descriptor Set Layout created by manager." << std::endl;

    // Create and use the pipeline factory
    // The pipeline layout is created inside createGraphicsPipeline if pipelineLayout is VK_NULL_HANDLE
       VkPushConstantRange pushConstantRange{};
       pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Model matrix used in vertex shader
       pushConstantRange.offset = 0;
       pushConstantRange.size = sizeof(glm::mat4); // Size of our model matrix
   
    pipelineFactory = std::make_unique<VulkanPipelineFactory>(m_vulkanDeviceRef);
       if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", 
                                                   "shaders/frag.spv", 
                                                   descriptorSetManager->getDescriptorSetLayout(), 
                                                   renderPass, pipelineLayout, graphicsPipeline,
                                                   &pushConstantRange)) { // Pass push constant range
        throw std::runtime_error("Failed to create graphics pipeline using factory!");
    }
    std::cout << "Graphics Pipeline and Layout created." << std::endl;

    // --- Create Wireframe Pipeline ---
    // The push constant range for the wireframe pipeline will also be for the model matrix.
    // It can be the same as the one used for the main graphics pipeline if the shader expects it at the same stage/offset/size.
    VkPushConstantRange wireframePushConstantRange{};
    wireframePushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Model matrix used in vertex shader
    wireframePushConstantRange.offset = 0;
    wireframePushConstantRange.size = sizeof(glm::mat4); // Size of our model matrix

    if (!pipelineFactory->createWireframePipeline("shaders/wireframe_vert.spv",
                                                  "shaders/wireframe_frag.spv",
                                                  descriptorSetManager->getDescriptorSetLayout(), // Same UBO layout
                                                  renderPass, // Same render pass
                                                  m_wireframePipelineLayout, m_wireframePipeline,
                                                  &wireframePushConstantRange)) {
        throw std::runtime_error("Failed to create wireframe pipeline using factory!");
    }
    std::cout << "Wireframe Pipeline and Layout created." << std::endl;

    swapChainManager->createFramebuffers(renderPass, depthImageView); // Create framebuffers (needs render pass, image views, and depth image view)
    // createCommandPool(); // Moved earlier

    // Create Texture Loader
    // Make sure you have a texture file at this path or change it // This is now handled by ResourceManager. We'll get the "dirt" texture for the initial cube.
    // textureLoader = std::make_unique<VulkanTextureLoader>(physicalDeviceRef, deviceRef, commandPool, graphicsQueueRef, "../resources/textures/dirt.png");
    // std::cout << "Texture Loader created." << std::endl;
    // This is now handled by ResourceManager. We'll get the "dirt" texture for the initial cube.

    // Create UBO resources
    bufferManager->createUniformBuffers(MAX_FRAMES_IN_FLIGHT, sizeof(UniformBufferObject), uniformBuffers, uniformBuffersMemory, uniformBuffersMapped);
    std::cout << "Uniform Buffers created." << std::endl;

    // Define descriptor pool sizes (previously in createDescriptorPool)
    // The manager's createDescriptorSets allocates swapChain->getImageCount() sets.
    uint32_t num_swap_chain_images = static_cast<uint32_t>(swapChainManager->getImageCount()); // Effectively MAX_FRAMES_IN_FLIGHT

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = num_swap_chain_images; // One UBO per frame in flight
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = num_swap_chain_images; // One Atlas Sampler per frame in flight


    // Convert std::array to std::vector for the function call if necessary,
    // or modify createDescriptorPool to accept an array or iterators.
    std::vector<VkDescriptorPoolSize> poolSizesVec(poolSizes.begin(), poolSizes.end());
    // Max sets is just the number of frames in flight, as we have one descriptor set (UBO+Atlas) per frame.
    uint32_t maxSetsForPool = num_swap_chain_images;
    descriptorSetManager->createDescriptorPool(poolSizesVec, maxSetsForPool);
    std::cout << "Descriptor Pool created by manager." << std::endl;

    // Allocate descriptor sets (previously in createDescriptorSets)
    descriptorSetManager->createDescriptorSets(); // Allocates descriptor sets for each frame in flight
    std::cout << "Descriptor Sets allocated by manager." << std::endl;

    // Update descriptor sets (this logic remains in VulkanRenderer as it's application-specific)
    // Now we bind the UBO and the Texture Atlas
    VkImageView atlasImageView = resourceManager->getAtlasImageView();
    VkSampler atlasSampler = resourceManager->getAtlasSampler();

    if (atlasImageView == VK_NULL_HANDLE || atlasSampler == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to get atlas image view or sampler from ResourceManager for init!");
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i]; // We update MAX_FRAMES_IN_FLIGHT UBOs
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSetManager->getDescriptorSets()[i]; // Use sets from manager
        descriptorWrites[0].dstBinding = 0; // UBO binding
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = atlasImageView; // Use the atlas image view
        imageInfo.sampler = atlasSampler;     // Use the atlas sampler

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSetManager->getDescriptorSets()[i];
        descriptorWrites[1].dstBinding = 1; // Texture sampler binding
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_vulkanDeviceRef.getLogicalDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    std::cout << "Descriptor Sets updated." << std::endl;

    // Process initial chunk changes (builds meshes for any existing/loaded chunks)
    processChunkChanges(); // This will also clear changed chunks in the world

    createCommandBuffers();
    std::cout << "Command Buffers created." << std::endl;
    createSyncObjects();
    std::cout << "Synchronization Objects created." << std::endl;
    std::cout << "VulkanRenderer initialization complete." << std::endl;

    // --- Finalize ImGui Initialization ---
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instanceRef;
    init_info.PhysicalDevice = m_vulkanDeviceRef.getPhysicalDevice();
    init_info.Device = m_vulkanDeviceRef.getLogicalDevice();
    init_info.QueueFamily = m_vulkanDeviceRef.getQueueFamilyIndices().graphicsFamily.value();
    init_info.Queue = m_vulkanDeviceRef.getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptorSetManager->getImguiDescriptorPool();
    init_info.MinImageCount = static_cast<uint32_t>(swapChainManager->getImageCount());
    init_info.ImageCount = static_cast<uint32_t>(swapChainManager->getImageCount());
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // init_info.CheckVkResultFn = check_vk_result; // Optional: can add a result checking function
    
    ImGui_ImplVulkan_Init(&init_info);
    std::cout << "ImGui Vulkan backend initialized." << std::endl;
 
    // Initialize deletion queues
    m_deletionQueues.resize(MAX_FRAMES_IN_FLIGHT);
}

// createSwapChain, createImageViews, createFramebuffers, cleanupSwapChain, recreateSwapChain
// chooseSwapSurfaceFormat, chooseSwapPresentMode, chooseSwapExtent are now handled by VulkanSwapChain

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainManager->getFormat(); // Get format from swap chain manager
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat; // Use the format found by BufferManager
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // We don't need to store depth after rendering
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1; // This is the second attachment
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef; // Add depth attachment reference

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_vulkanDeviceRef.getLogicalDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
}
void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_vulkanDeviceRef.getQueueFamilyIndices().graphicsFamily.value();

    if (vkCreateCommandPool(m_vulkanDeviceRef.getLogicalDevice(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

void VulkanRenderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(m_vulkanDeviceRef.getLogicalDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers!");
    }
}

void VulkanRenderer::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    // renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT); // Removed
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    // presentationFinishedSemaphores are sized by swapchain image count, and resized in init/recreateSwapChainResources.

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_vulkanDeviceRef.getLogicalDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_vulkanDeviceRef.getLogicalDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }
    // Create presentation semaphores (one per swapchain image)
    for (size_t i = 0; i < presentationFinishedSemaphores.size(); ++i) {
        if (vkCreateSemaphore(m_vulkanDeviceRef.getLogicalDevice(), &semaphoreInfo, nullptr, &presentationFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create presentation synchronization objects!");
        }
    }
}

// --- Graphics Pipeline Creation ---

// --- Buffer Creation ---

// --- Update Uniform Buffer ---

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};
    // Get view and projection matrices from the camera
    // Pass the current rebase origin to getViewMatrix
    glm::ivec3 rebaseOriginChunkCoord = m_world.getRebaseOriginChunkCoord();
    ubo.view = m_camera->getViewMatrix(rebaseOriginChunkCoord);
    float aspectRatio = swapChainManager->getExtent().width / (float)swapChainManager->getExtent().height;
       ubo.proj = m_camera->getProjectionMatrix(aspectRatio);    
       // ubo.model = glm::mat4(1.0f); // Removed, model matrix handled by push constants

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    std::cout << "[RCB] start. cmdBuf=" << reinterpret_cast<uintptr_t>(commandBuffer)
              << " imageIndex=" << imageIndex
              << " currentFrame=" << currentFrame
              << " renderPass=" << reinterpret_cast<uintptr_t>(renderPass)
              << " framebuffer=" << reinterpret_cast<uintptr_t>(swapChainManager->getFramebuffer(imageIndex))
              << std::endl;

    if (commandBuffer == VK_NULL_HANDLE) {
        std::cerr << "[RCB][ERR] commandBuffer is VK_NULL_HANDLE\n";
        return;
    }
    if (renderPass == VK_NULL_HANDLE) {
        std::cerr << "[RCB][ERR] renderPass is VK_NULL_HANDLE\n";
        return;
    }
    VkFramebuffer fb = swapChainManager->getFramebuffer(imageIndex);
    if (fb == VK_NULL_HANDLE) {
        std::cerr << "[RCB][ERR] framebuffer for imageIndex " << imageIndex << " is VK_NULL_HANDLE\n";
        return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    std::cout << "[RCB] before vkBeginCommandBuffer\n";
    VkResult r = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    std::cout << "[RCB] vkBeginCommandBuffer -> " << r << std::endl;
    if (r != VK_SUCCESS) {
        std::cerr << "[RCB][ERR] vkBeginCommandBuffer failed: " << r << std::endl;
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = fb;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainManager->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.39f, 0.58f, 0.93f, 1.0f}}; // Clear color for attachment 0
    clearValues[1].depthStencil = {1.0f, 0};             // Clear depth to 1.0 (farthest), stencil to 0 for attachment 1

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    std::cout << "[RCB] before vkCmdBeginRenderPass\n";
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    std::cout << "[RCB] after vkCmdBeginRenderPass\n";

    // Set dynamic viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainManager->getExtent().width);
    viewport.height = static_cast<float>(swapChainManager->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    std::cout << "[RCB] viewport set\n";

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainManager->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    std::cout << "[RCB] scissor set\n";

    // Pipeline bind
    if (graphicsPipeline == VK_NULL_HANDLE) {
        std::cerr << "[RCB][ERR] graphicsPipeline is VK_NULL_HANDLE\n";
    } else {
        std::cout << "[RCB] before vkCmdBindPipeline pipeline=" << reinterpret_cast<uintptr_t>(graphicsPipeline) << std::endl;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        std::cout << "[RCB] after vkCmdBindPipeline\n";
    }

    // Descriptor set binding - validate indices
    auto &descSets = descriptorSetManager->getDescriptorSets();
    if (static_cast<size_t>(currentFrame) >= descSets.size()) {
        std::cerr << "[RCB][ERR] descriptor sets size " << descSets.size() << " <= currentFrame " << currentFrame << std::endl;
    } else {
        VkDescriptorSet ds = descSets[currentFrame];
        std::cout << "[RCB] before vkCmdBindDescriptorSets ds=" << reinterpret_cast<uintptr_t>(ds) << std::endl;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
        std::cout << "[RCB] after vkCmdBindDescriptorSets\n";
    }

    // Iterate through chunk render data and issue draw calls
    std::cout << "[RCB] chunkRenderData count=" << m_chunkRenderData.size() << std::endl;
    for (const auto& pair : m_chunkRenderData) {
        const ChunkRenderData& chunkData = pair.second;
        std::cout << "[RCB] chunk: vb=" << reinterpret_cast<uintptr_t>(chunkData.vertexBuffer)
                  << " ib=" << reinterpret_cast<uintptr_t>(chunkData.indexBuffer)
                  << " idxCount=" << chunkData.indexCount
                  << " vOffset=" << chunkData.vertexOffset
                  << " iOffset=" << chunkData.indexOffset << std::endl;

        if (chunkData.vertexBuffer == VK_NULL_HANDLE || chunkData.indexBuffer == VK_NULL_HANDLE || chunkData.indexCount == 0) {
            std::cout << "[RCB] skipping empty/invalid chunk\n";
            continue;
        }

        VkBuffer vertexBuffers[] = { chunkData.vertexBuffer };
        VkDeviceSize bindOffsets[] = { chunkData.vertexOffset };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, bindOffsets);
        vkCmdBindIndexBuffer(commandBuffer, chunkData.indexBuffer, chunkData.indexOffset, VK_INDEX_TYPE_UINT32);

        // Push constants - validate pipelineLayout
        if (pipelineLayout == VK_NULL_HANDLE) {
            std::cerr << "[RCB][ERR] pipelineLayout is VK_NULL_HANDLE, skipping push constants\n";
        } else {
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &chunkData.modelMatrix);
        }

        vkCmdDrawIndexed(commandBuffer, chunkData.indexCount, 1, 0, 0, 0);
    }
    std::cout << "[RCB] finished drawing chunks\n";

    // Wireframe section guarded
    if (m_wireframeIndexCount > 0 && m_wireframeVertexBuffer != VK_NULL_HANDLE && m_wireframeIndexBuffer != VK_NULL_HANDLE) {
        std::cout << "[RCB] drawing wireframe vb=" << reinterpret_cast<uintptr_t>(m_wireframeVertexBuffer) << " ib=" << reinterpret_cast<uintptr_t>(m_wireframeIndexBuffer) << std::endl;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_wireframePipeline);
        VkBuffer wireframeVertexBuffers[] = {m_wireframeVertexBuffer};
        VkDeviceSize wireOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, wireframeVertexBuffers, wireOffsets);
        vkCmdBindIndexBuffer(commandBuffer, m_wireframeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(commandBuffer, m_wireframePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_wireframeModelMatrix);

        // Use the offsets when drawing the wireframe
        uint32_t firstIndex = static_cast<uint32_t>(m_wireframeIndexOffset / sizeof(uint32_t));
        int32_t vertexOffset = static_cast<int32_t>(m_wireframeVertexOffset / sizeof(WireframeMesher::WireframeVertex));
        vkCmdDrawIndexed(commandBuffer, m_wireframeIndexCount, 1, firstIndex, vertexOffset, 0); // Use calculated offsets
    }

    vkCmdEndRenderPass(commandBuffer);
    std::cout << "[RCB] after vkCmdEndRenderPass\n";

    // ImGui draw - guard null
    ImDrawData* draw_data = ImGui::GetDrawData();
    std::cout << "[RCB] ImGui draw_data ptr=" << reinterpret_cast<uintptr_t>(draw_data) << std::endl;
    if (draw_data != nullptr) {
        std::cout << "[RCB] before ImGui_ImplVulkan_RenderDrawData\n";
        ImGui_ImplVulkan_RenderDrawData(draw_data, commandBuffer);
        std::cout << "[RCB] after ImGui_ImplVulkan_RenderDrawData\n";
    } else {
        std::cout << "[RCB] skipping ImGui draw (null draw data)\n";
    }

    std::cout << "[RCB] before vkEndCommandBuffer\n";
    VkResult endR = vkEndCommandBuffer(commandBuffer);
    std::cout << "[RCB] vkEndCommandBuffer -> " << endR << std::endl;
    if (endR != VK_SUCCESS) {
        std::cerr << "[RCB][ERR] vkEndCommandBuffer failed: " << endR << std::endl;
    } else {
        std::cout << "[RCB] recordCommandBuffer complete\n";
    }
}

void VulkanRenderer::drawFrame() {
    // Debug instrumentation: print key state so we can see what causes the crash.
    std::cout << "[VKR] drawFrame start. currentFrame=" << currentFrame
              << " commandBuffers=" << commandBuffers.size()
              << " inFlightFences=" << inFlightFences.size()
              << " imageAvailableSemaphores=" << imageAvailableSemaphores.size()
              << " presentationFinishedSemaphores=" << presentationFinishedSemaphores.size()
              << " imagesInFlight=" << imagesInFlight.size()
              << std::endl;

    if (commandBuffers.size() <= static_cast<size_t>(currentFrame)) {
        std::cerr << "[VKR][ERR] commandBuffers.size() <= currentFrame -> " << commandBuffers.size() << " <= " << currentFrame << std::endl;
        return;
    }
    if (inFlightFences.size() <= static_cast<size_t>(currentFrame)) {
        std::cerr << "[VKR][ERR] inFlightFences not initialized for currentFrame: " << currentFrame << std::endl;
        return;
    }

    // Print handles (safe to print even if VK_NULL_HANDLE)
    std::cout << "[VKR] handles: fence=" << reinterpret_cast<uintptr_t>(inFlightFences[currentFrame])
              << " imgAvail=" << (imageAvailableSemaphores.size() > static_cast<size_t>(currentFrame) ? reinterpret_cast<uintptr_t>(imageAvailableSemaphores[currentFrame]) : 0)
              << std::endl;

    // If fence is VK_NULL_HANDLE, avoid calling vkWaitForFences (prevents crashing on invalid/uninitialized handles).
    if (inFlightFences[currentFrame] != VK_NULL_HANDLE) {
        VkResult r = vkWaitForFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        std::cout << "[VKR] vkWaitForFences returned " << r << std::endl;
    } else {
        std::cout << "[VKR] inFlightFences[currentFrame] is VK_NULL_HANDLE, skipping wait\n";
    }

    std::cout << "[VKR] about to acquireNextImage\n";
    uint32_t imageIndex = UINT32_MAX;
    VkResult result = swapChainManager->acquireNextImage((imageAvailableSemaphores.size() > static_cast<size_t>(currentFrame) ? imageAvailableSemaphores[currentFrame] : VK_NULL_HANDLE), &imageIndex);
    std::cout << "[VKR] acquireNextImage returned result=" << result << " imageIndex=" << imageIndex << std::endl;

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChainResources();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    // --- Corrected Deferred Deletion ---
    // At the start of the frame, we have already waited on inFlightFences[currentFrame].
    // This means the GPU has finished with the resources from the last time this frame index was used.
    // It is now safe to process the deletion queue for the current frame index.
    for (const auto& resource : m_deletionQueues[currentFrame]) {
        if (resource.type == ResourceToDelete::Type::VertexBufferRegion) {
            bufferManager->freeVertexBuffer(resource.bufferHandle, resource.offset, resource.size);
        } else if (resource.type == ResourceToDelete::Type::IndexBufferRegion) {
            bufferManager->freeIndexBuffer(resource.bufferHandle, resource.offset, resource.size);
        } else if (resource.type == ResourceToDelete::Type::Buffer) {
            if (resource.bufferHandle != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.bufferHandle, nullptr);
            if (resource.memoryHandle != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memoryHandle, nullptr);
        }
    }
    m_deletionQueues[currentFrame].clear();

    // If a rebase occurred, update all chunk model matrices immediately
    if (m_world.rebaseOccurredLastFrame()) {
        // std::cout << "Rebase detected by renderer. Updating all model matrices." << std::endl;
        glm::ivec3 newRebaseOrigin = m_world.getRebaseOriginChunkCoord();
        for (auto& pair : m_chunkRenderData) {
            const glm::ivec3& chunkCoord = pair.first;
            ChunkRenderData& renderData = pair.second;
            glm::ivec3 relativeChunkCoord = chunkCoord - newRebaseOrigin;
            renderData.modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(relativeChunkCoord.x * CHUNK_SIDE_LENGTH,
                                                                               relativeChunkCoord.y * CHUNK_SIDE_LENGTH,
                                                                               relativeChunkCoord.z * CHUNK_SIDE_LENGTH));
        }
    }

    // Update the wireframe for the targeted block FIRST to generate mesh data if needed
    updateTargetedBlockWireframe();

    // Process chunk changes (mesh rebuilds for modified/new chunks, cleanup for unloaded)
    processChunkChanges();

    // After acquiring the image, we might have waited on an old fence.
    // Now, mark the image as being in use by the *current* frame's fence.
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        VkResult r = vkWaitForFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        std::cout << "[VKR] vkWaitForFences(imagesInFlight) returned " << r << std::endl;
    }

    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    // Check UBO mapping before updating
    if (static_cast<size_t>(currentFrame) >= uniformBuffersMapped.size()) {
        std::cerr << "[VKR][ERR] uniformBuffersMapped too small: " << uniformBuffersMapped.size() << " <= " << currentFrame << std::endl;
    } else if (uniformBuffersMapped[currentFrame] == nullptr) {
        std::cerr << "[VKR][ERR] uniformBuffersMapped[" << currentFrame << "] is nullptr, skipping update\n";
    } else {
        std::cout << "[VKR] calling updateUniformBuffer(" << currentFrame << ")\n";
        updateUniformBuffer(currentFrame);
        std::cout << "[VKR] updateUniformBuffer done\n";
    }

    // Reset the fence for the current frame
    {
        VkResult r = vkResetFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &inFlightFences[currentFrame]);
        std::cout << "[VKR] vkResetFences returned " << r << std::endl;
    }

    // Reset and record command buffer
    {
        VkResult r = vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        std::cout << "[VKR] vkResetCommandBuffer returned " << r << " for cmdBuf=" << reinterpret_cast<uintptr_t>(commandBuffers[currentFrame]) << std::endl;
    }

    try {
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
        std::cout << "[VKR] recordCommandBuffer succeeded\n";
    } catch (const std::exception& e) {
        std::cerr << "[VKR][ERR] recordCommandBuffer threw: " << e.what() << std::endl;
        return;
    } catch (...) {
        std::cerr << "[VKR][ERR] recordCommandBuffer threw unknown exception\n";
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    // signal semaphore is tied to imageIndex
    if (imageIndex >= presentationFinishedSemaphores.size()) {
        std::cerr << "[VKR][WARN] imageIndex >= presentationFinishedSemaphores.size(): " << imageIndex << " >= " << presentationFinishedSemaphores.size() << std::endl;
        // Resize to avoid OOB when debugging
        presentationFinishedSemaphores.resize(imageIndex + 1, VK_NULL_HANDLE);
    }
    VkSemaphore signalSemaphores[] = {presentationFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    std::cout << "[VKR] about to vkQueueSubmit on graphicsQueue\n";
    VkResult submitRes = vkQueueSubmit(m_vulkanDeviceRef.getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]);
    std::cout << "[VKR] vkQueueSubmit returned " << submitRes << std::endl;
    if (submitRes != VK_SUCCESS) {
        std::cerr << "[VKR][ERR] vkQueueSubmit failed: " << submitRes << std::endl;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    VkSemaphore presentWaitSemaphores[] = {presentationFinishedSemaphores[imageIndex]};
    presentInfo.pWaitSemaphores = presentWaitSemaphores;
    VkSwapchainKHR swapChains[] = {swapChainManager->getSwapChainHandle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    std::cout << "[VKR] about to vkQueuePresentKHR on presentQueue\n";
    VkResult presentRes = vkQueuePresentKHR(m_vulkanDeviceRef.getPresentQueue(), &presentInfo);
    std::cout << "[VKR] vkQueuePresentKHR returned " << presentRes << std::endl;

    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChainResources();
    } else if (presentRes != VK_SUCCESS) {
        std::cerr << "[VKR][ERR] vkQueuePresentKHR failed: " << presentRes << std::endl;
        return;
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::cleanupDepthResources() {
    if (depthImageView != VK_NULL_HANDLE) vkDestroyImageView(m_vulkanDeviceRef.getLogicalDevice(), depthImageView, nullptr);
    if (depthImage != VK_NULL_HANDLE) vkDestroyImage(m_vulkanDeviceRef.getLogicalDevice(), depthImage, nullptr);
    if (depthImageMemory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), depthImageMemory, nullptr);
    depthImageView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthImageMemory = VK_NULL_HANDLE;
}

void VulkanRenderer::recreateSwapChainResources() {
    std::cout << "Recreating swap chain dependent resources..." << std::endl;

    // Wait for the device to be idle before cleanup/recreation
    vkDeviceWaitIdle(m_vulkanDeviceRef.getLogicalDevice());

    // 1. Cleanup old swap chain resources (swap chain, image views, framebuffers)
    // Framebuffers are cleaned by swapChainManager as they depend on swap chain image views
    swapChainManager->cleanupForRecreation();

    // Process all pending deletions immediately since we've waited for idle
    std::cout << "RecreateSwapChainResources: Processing all deletion queues..." << std::endl;
    for (size_t i = 0; i < m_deletionQueues.size(); ++i) {
        for (const auto& resource : m_deletionQueues[i]) {
            if (resource.type == ResourceToDelete::Type::Buffer) {
                if (resource.bufferHandle != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.bufferHandle, nullptr);
                if (resource.memoryHandle != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memoryHandle, nullptr);
            } else if (resource.type == ResourceToDelete::Type::VertexBufferRegion) {
                bufferManager->freeVertexBuffer(resource.bufferHandle, resource.offset, resource.size);
            } else if (resource.type == ResourceToDelete::Type::IndexBufferRegion) {
                bufferManager->freeIndexBuffer(resource.bufferHandle, resource.offset, resource.size);
            }
        }
        m_deletionQueues[i].clear();
    }

    // 2. Cleanup old depth buffer resources (as they depend on extent)
    cleanupDepthResources();

    // 3. Cleanup renderer resources dependent on the swap chain/render pass
    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vulkanDeviceRef.getLogicalDevice(), graphicsPipeline, nullptr); // Nullify handles after destruction
    if (m_wireframePipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vulkanDeviceRef.getLogicalDevice(), m_wireframePipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vulkanDeviceRef.getLogicalDevice(), pipelineLayout, nullptr);
    if (m_wireframePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vulkanDeviceRef.getLogicalDevice(), m_wireframePipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_vulkanDeviceRef.getLogicalDevice(), renderPass, nullptr);

    graphicsPipeline = VK_NULL_HANDLE; // Nullify handles after destruction
    m_wireframePipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    m_wireframePipelineLayout = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;

    // 4. Recreate swap chain and image views
    swapChainManager->createSwapChainInternal(); // Creates swap chain, gets new format/extent
    swapChainManager->createImageViews();
    std::cout << "Swap chain and image views recreated by manager." << std::endl;

    // 4.5. Re-initialize imagesInFlight fences as image count might have changed
    // Fences in inFlightFences are not destroyed, so we can reuse them.
    imagesInFlight.assign(swapChainManager->getImageCount(), VK_NULL_HANDLE);

    // Destroy existing presentation semaphores before recreating them.
    // These semaphores are tied to the number of swap chain images, which might change.
    for (auto semaphore : presentationFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), semaphore, nullptr);
        }
    }
    // The vector will be cleared and resized, or just resized, then repopulated by createSyncObjects.

    // Re-initialize presentationFinishedSemaphores as image count might have changed
    // Old semaphores were cleaned up in cleanupDepthResources (or should be if not already)
    // We resize and then createSyncObjects will recreate them.
    presentationFinishedSemaphores.resize(swapChainManager->getImageCount(), VK_NULL_HANDLE);
    // createSyncObjects(); // This will recreate all semaphores and fences, including the newly sized presentationFinishedSemaphores

    // 5. Recreate depth buffer resources (depends on new swap chain extent)
    bufferManager->createDepthResources(swapChainManager->getExtent(), depthImage, depthImageMemory, depthImageView, depthFormat);
    std::cout << "Depth Resources recreated." << std::endl;

    // 6. Recreate render pass (depends on new format and new depth format)
    createRenderPass();
    std::cout << "Render pass recreated." << std::endl;

    // 7. Recreate graphics pipeline (depends on new render pass and existing push constant setup)
    // Define the push constant range again, as it's needed for pipeline recreation
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);
    if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", "shaders/frag.spv", descriptorSetManager->getDescriptorSetLayout(), renderPass, pipelineLayout, graphicsPipeline, &pushConstantRange)) {
        throw std::runtime_error("Failed to recreate graphics pipeline using factory!");
    }
    std::cout << "Graphics pipeline recreated." << std::endl;

    // 7.5 Recreate wireframe pipeline (depends on new render pass and existing push constant setup)
    VkPushConstantRange wireframePushConstantRange{};
    wireframePushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    wireframePushConstantRange.offset = 0;
    wireframePushConstantRange.size = sizeof(glm::mat4);
    if (!pipelineFactory->createWireframePipeline("shaders/wireframe_vert.spv",
                                                  "shaders/wireframe_frag.spv",
                                                  descriptorSetManager->getDescriptorSetLayout(),
                                                  renderPass,
                                                  m_wireframePipelineLayout, m_wireframePipeline,
                                                  &wireframePushConstantRange)) {
        throw std::runtime_error("Failed to recreate wireframe pipeline using factory!");
    }
    std::cout << "Wireframe pipeline recreated." << std::endl;

    // 8. Recreate framebuffers (depends on new image views, render pass, and new depth image view)
    swapChainManager->createFramebuffers(renderPass, depthImageView);

    // Command buffers need to be re-recorded because they reference the old framebuffers.
    // We don't explicitly recreate them here because the drawFrame loop resets and
    // re-records the command buffer for the current frame anyway.
    // However, sync objects need to be recreated.
    // Destroy MAX_FRAMES_IN_FLIGHT sync objects before calling createSyncObjects,
    // as createSyncObjects will unconditionally recreate them.
    // presentationFinishedSemaphores are handled by their own loop earlier.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        }
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_vulkanDeviceRef.getLogicalDevice(), inFlightFences[i], nullptr);
        }
    }
    // However, sync objects need to be recreated if their count changed or if they were destroyed.
    createSyncObjects(); // Recreate all sync objects, including presentationFinishedSemaphores

    std::cout << "Swap chain dependent resources fully recreated." << std::endl;
}

void VulkanRenderer::destroyChunkRenderData(ChunkRenderData& data) {
    if (data.vertexBuffer != VK_NULL_HANDLE) {
        // Queue the REGION for deletion, not the whole buffer.
        m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::VertexBufferRegion, data.vertexBuffer, VK_NULL_HANDLE, data.vertexOffset, data.vertexSize});
        data.vertexBuffer = VK_NULL_HANDLE;
    }
    if (data.indexBuffer != VK_NULL_HANDLE) {
        // Queue the REGION for deletion.
        m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::IndexBufferRegion, data.indexBuffer, VK_NULL_HANDLE, data.indexOffset, data.indexSize});
        data.indexBuffer = VK_NULL_HANDLE;
    }
    data.indexCount = 0;
}

// This function now takes MeshData directly, to be called when an async task completes.
void VulkanRenderer::createChunkRenderDataFromMeshData(VkCommandBuffer& transferCommandBuffer, const glm::ivec3& chunkCoord, const ModelData& meshData) {
    if (!bufferManager || !resourceManager) {
        std::cerr << "VulkanRenderer::createChunkRenderDataFromMeshData: Missing bufferManager or resourceManager." << std::endl;
        return;
    }

    // MeshData is now passed in

    // Ensure old data for this chunkCoord is cleaned up if it exists
    auto it = m_chunkRenderData.find(chunkCoord);
    if (it != m_chunkRenderData.end()) {
        destroyChunkRenderData(it->second); // Clean up old buffers
        // m_chunkRenderData.erase(it); // No, we'll overwrite or emplace
    }

    if (!meshData.vertices.empty() && !meshData.indices.empty()) {
        ChunkRenderData& renderData = m_chunkRenderData[chunkCoord]; // Creates if not exists, or gets reference
        renderData.vertexSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
        renderData.indexSize = sizeof(meshData.indices[0]) * meshData.indices.size();

        // Allocate regions from the buffer manager
        renderData.vertexBuffer = bufferManager->createVertexBuffer(transferCommandBuffer, meshData.vertices, renderData.vertexOffset);
        renderData.indexBuffer = bufferManager->createIndexBuffer(transferCommandBuffer, meshData.indices, renderData.indexOffset);
        
        renderData.indexCount = static_cast<uint32_t>(meshData.indices.size());

        // Calculate the chunk's position relative to the current rebase origin
        glm::ivec3 rebaseOriginChunkCoord = m_world.getRebaseOriginChunkCoord();
        glm::ivec3 relativeChunkCoord = chunkCoord - rebaseOriginChunkCoord;
        glm::vec3 relativeWorldPos = glm::vec3(relativeChunkCoord.x * CHUNK_SIDE_LENGTH,
                                               relativeChunkCoord.y * CHUNK_SIDE_LENGTH,
                                               relativeChunkCoord.z * CHUNK_SIDE_LENGTH);
        renderData.modelMatrix = glm::translate(glm::mat4(1.0f), relativeWorldPos);
        // std::cout << "Created render data for chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << " Indices: " << renderData.indexCount << std::endl;
    } else {
        // If mesh is empty, ensure no render data exists or it's cleared
        if (it != m_chunkRenderData.end()) { // If it existed
             m_chunkRenderData.erase(it); // Remove the entry as it's now empty
        }
        // std::cout << "No mesh data to create for chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << std::endl;
    }
}

void VulkanRenderer::processChunkChanges() {
    // IMPORTANT: The vkDeviceWaitIdle() call here is a major performance bottleneck
    // for frequent updates. Removing it requires a more sophisticated system for
    // managing buffer lifetimes (e.g., deferred deletion queues for buffers that
    // might still be in use by the GPU). For this optimization step, we are
    // focusing on offloading CPU meshing work. The vkDeviceWaitIdle might still
    // cause hitches if buffer creation/destruction is frequent.
    // vkDeviceWaitIdle(m_vulkanDeviceRef.getLogicalDevice()); // Kept for now for safety, but a target for future optimization.
    if (!m_threadManager) return;

    // At the start of processing changes, check for and clean up any transfers that have finished.
    // This recycles command buffers and fences for future use.
    bufferManager->checkAndCleanupCompletedTransfers();

    bool hasChunkMeshResults = m_threadManager->hasMeshResults();

    // --- Batch Upload Logic ---
    // A batch operation is needed if we have new chunk meshes OR if the wireframe needs an update.
    VkCommandBuffer transferCommandBuffer = VK_NULL_HANDLE;
    if (hasChunkMeshResults || m_wireframeMeshNeedsUpdate) {
        transferCommandBuffer = bufferManager->beginTransferCommands();
    }

    // --- Stage 1: Process completed meshes from the thread manager ---
    if (hasChunkMeshResults) {
        while (auto resultOpt = m_threadManager->getMeshResult()) {
            const auto& result = *resultOpt;
            createChunkRenderDataFromMeshData(transferCommandBuffer, result.chunkCoord, result.meshData); // Pass by reference
            m_meshingTasksInProgress.erase(result.chunkCoord);
        }
    }

    // --- Stage 1.5: Process wireframe update if needed, using the same command buffer ---
    if (m_wireframeMeshNeedsUpdate && transferCommandBuffer != VK_NULL_HANDLE) {
        // Use the mesh data that was generated and stored by updateTargetedBlockWireframe.
        if (m_pendingWireframeMeshData) {
            const auto& meshData = *m_pendingWireframeMeshData;

            if (!meshData.vertices.empty() && !meshData.indices.empty()) {
                m_wireframeVertexBuffer = bufferManager->createVertexBuffer(transferCommandBuffer, meshData.vertices, m_wireframeVertexOffset);
                m_wireframeIndexBuffer = bufferManager->createIndexBuffer(transferCommandBuffer, meshData.indices, m_wireframeIndexOffset);
            }
        }
        m_wireframeMeshNeedsUpdate = false; // Mark as updated
        m_pendingWireframeMeshData.reset(); // Clear the stored data
    }

    if (transferCommandBuffer != VK_NULL_HANDLE) {
        bufferManager->endAndSubmitTransferCommands(transferCommandBuffer);
    }

    // --- Stage 2: Submit new meshing jobs to the thread manager ---
    const auto& changedChunks = m_world.getChangedChunks();
    std::set<glm::ivec3, IVec3Comparator> chunksToActuallyProcess = changedChunks; // Copy to iterate

    for (const glm::ivec3& chunkCoord : chunksToActuallyProcess) {
        // Check if a task for this chunk is already in progress
        if (m_meshingTasksInProgress.count(chunkCoord)) {
            // A task is already in progress. Acknowledge it so it's removed
            // from the world's changed set, but don't submit a duplicate job.
            m_world.acknowledgeChunkChangeProcessed(chunkCoord);
            continue;
        }

        std::shared_ptr<Chunk> chunk_sptr = m_world.getChunk(chunkCoord);

        if (chunk_sptr) { // Chunk exists: modified or newly loaded, needs meshing
            m_threadManager->submitMeshJob(chunkCoord);
            m_meshingTasksInProgress.insert(chunkCoord);
            m_world.acknowledgeChunkChangeProcessed(chunkCoord); // Mark as processed
        } else { // Chunk does not exist in world map: it was unloaded
            auto renderDataIt = m_chunkRenderData.find(chunkCoord);
            if (renderDataIt != m_chunkRenderData.end()) {
                destroyChunkRenderData(renderDataIt->second);
                m_chunkRenderData.erase(renderDataIt);
            }
            m_world.acknowledgeChunkChangeProcessed(chunkCoord); // Mark as processed (unloaded)
        }
    }
}

void VulkanRenderer::updateTargetedBlockWireframe() {
    const auto& targetedBlockInfoOpt = m_playerRef.getCurrentTargetedBlockInfo();
    // bool needsBufferUpdate = false; // This variable is no longer needed with the new structure

    // Handle the case where no block is targeted or the ray didn't hit
    if (!targetedBlockInfoOpt || !targetedBlockInfoOpt->hit) {
        if (m_lastTargetedBlockPos) { // If we were previously targeting a block
            // We need to clear the buffers and reset state
            m_lastTargetedBlockPos = std::nullopt;
            m_wireframeMeshNeedsUpdate = true; // Ready for an update when a target is acquired
            
            // vkDeviceWaitIdle removed. Queue wireframe buffers for deferred deletion.
            if (m_wireframeVertexBuffer != VK_NULL_HANDLE) {
                // Queue the old REGION for deletion
                m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::VertexBufferRegion, m_wireframeVertexBuffer, VK_NULL_HANDLE, m_wireframeVertexOffset, m_wireframeVertexSize});
            }
            if (m_wireframeIndexBuffer != VK_NULL_HANDLE) {
                m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::IndexBufferRegion, m_wireframeIndexBuffer, VK_NULL_HANDLE, m_wireframeIndexOffset, m_wireframeIndexSize});
            }
            m_wireframeVertexBuffer = VK_NULL_HANDLE;
            m_wireframeIndexBuffer = VK_NULL_HANDLE;
            m_wireframeIndexCount = 0;
        }
        return; // Early return as there's no wireframe to update or draw
    }

    // At this point, a block IS targeted (targetedBlockInfoOpt is valid and hit is true)
    const RaycastResult& targetedBlockInfo = *targetedBlockInfoOpt; // Safe to dereference

    // If the target hasn't changed and no update is forced, do nothing.
    // Condition: (m_lastTargetedBlockPos IS valid AND current target IS THE SAME AS last AND m_wireframeMeshNeedsUpdate IS false)
    if (m_lastTargetedBlockPos && 
        (*m_lastTargetedBlockPos == targetedBlockInfo.blockPosition) && 
        !m_wireframeMeshNeedsUpdate) {
        return; // No change needed
    }

    // --- Proceed with updating the wireframe mesh and buffers ---
    m_lastTargetedBlockPos = targetedBlockInfo.blockPosition;
    m_wireframeMeshNeedsUpdate = true; // Set the flag so processChunkChanges will handle the update.

    // The rest of this function now only queues old buffers for deletion and sets the model matrix.

    // Query the World for the block ID using the position from RaycastResult
    uint16_t blockID_from_world = m_world.getBlockID(targetedBlockInfo.blockPosition);
    const Block* blockDef = m_blockRegistryRef.getBlockDefinition(blockID_from_world);
    Physics::VoxelShape shapeToMesh;

    if (blockDef && blockDef->getCustomShape()) {
        shapeToMesh = *(blockDef->getCustomShape());
    } else {
        // Default full block shape if no custom shape or blockDef is null (should not happen for valid ID)
        shapeToMesh = Physics::VoxelShape::createCuboidShape(0.0f, 0.0f, 0.0f, 16.0f, 16.0f, 16.0f);
    }

    // Queue old buffer regions for deletion
    if (m_wireframeVertexBuffer != VK_NULL_HANDLE) {
        m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::VertexBufferRegion, m_wireframeVertexBuffer, VK_NULL_HANDLE, m_wireframeVertexOffset, m_wireframeVertexSize});
    }
    if (m_wireframeIndexBuffer != VK_NULL_HANDLE) {
        m_deletionQueues[currentFrame].push_back({ResourceToDelete::Type::IndexBufferRegion, m_wireframeIndexBuffer, VK_NULL_HANDLE, m_wireframeIndexOffset, m_wireframeIndexSize});
    }
    
    // Generate the mesh data ONCE and store it for processChunkChanges to use.
    m_pendingWireframeMeshData = WireframeMesher::generateVoxelShapeMesh(shapeToMesh);
    if (m_pendingWireframeMeshData && !m_pendingWireframeMeshData->indices.empty()) {
        // Store the size of the data we are about to allocate
        m_wireframeVertexSize = sizeof(m_pendingWireframeMeshData->vertices[0]) * m_pendingWireframeMeshData->vertices.size();
        m_wireframeIndexSize = sizeof(m_pendingWireframeMeshData->indices[0]) * m_pendingWireframeMeshData->indices.size();

        // Create new regions using the sub-allocator

        m_wireframeIndexCount = static_cast<uint32_t>(m_pendingWireframeMeshData->indices.size());
    } else {
        m_wireframeIndexCount = 0;
    }

    // Calculate model matrix
    glm::i64vec3 worldBlockPos_i64 = targetedBlockInfo.blockPosition;
    glm::ivec3 rebaseOriginChunkCoord_ivec3 = m_world.getRebaseOriginChunkCoord();
    glm::i64vec3 rebaseOriginWorldPos_i64 = glm::i64vec3(rebaseOriginChunkCoord_ivec3) * glm::i64vec3(CHUNK_SIDE_LENGTH, CHUNK_SIDE_LENGTH, CHUNK_SIDE_LENGTH);
    glm::i64vec3 relativeBlockPos_i64 = worldBlockPos_i64 - rebaseOriginWorldPos_i64;
    m_wireframeModelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(relativeBlockPos_i64));
}