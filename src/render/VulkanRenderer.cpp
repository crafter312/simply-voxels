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
#include "../physics/WireframeMesher.hpp" // For WireframeMesher
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

// Initialize MAX_CONCURRENT_MESHING_TASKS
// This needs to be done in the .cpp file because std::thread::hardware_concurrency() is not constexpr.
const size_t VulkanRenderer::MAX_CONCURRENT_MESHING_TASKS = []() {
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) {
        // hardware_concurrency() couldn't determine or no concurrency. Default to a sensible value.
        return 4u; // Default to 4 tasks
    }
    return std::max(1u, num_cores - 1); // Use num_cores - 1, but at least 1
}();

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

    // Ensure all asynchronous meshing tasks are complete before destroying resources they might use.
    // This is crucial because async tasks capture references to world and resourceManager.
    if (!m_pendingMeshFutures.empty()) {
        std::cout << "VulkanRenderer Destructor: Waiting for " << m_pendingMeshFutures.size() << " pending mesh tasks to complete..." << std::endl;
        for (auto& future : m_pendingMeshFutures) {
            if (future.second.valid()) { // Check if the future is valid (hasn't had .get() called on it yet)
                try {
                    future.second.get(); // This will block until the task is finished.
                                  // It will also re-throw any exception thrown by the async task.
                } catch (const std::exception& e) {
                    std::cerr << "VulkanRenderer Destructor: Exception caught from an async mesh task: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "VulkanRenderer Destructor: Unknown exception caught from an async mesh task." << std::endl;
                }
            }
        }
        m_pendingMeshFutures.clear(); // All tasks are now complete, clear the vector.
        std::cout << "VulkanRenderer Destructor: All pending mesh tasks are done." << std::endl;
    }
    // Note: vkDeviceWaitIdle should be called before this destructor is invoked (e.g., in HelloVulkanApp::cleanup)

    // Destroy all per-chunk render data first. This will queue their Vulkan buffers for deletion.
    destroyAllChunkRenderData();

    // Process any remaining items in deletion queues.
    // Assuming vkDeviceWaitIdle has been called externally or we'd call it here.
    std::cout << "VulkanRenderer Destructor: Processing final deletion queues..." << std::endl;
    for (size_t i = 0; i < m_deletionQueues.size(); ++i) {
        for (const auto& resource : m_deletionQueues[i]) {
            if (resource.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.buffer, nullptr);
            if (resource.memory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memory, nullptr);
        }
        m_deletionQueues[i].clear();
    }

    // Swap chain resources are cleaned up by swapChainManager's destructor
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
    if (m_wireframeVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), m_wireframeVertexBuffer, nullptr);
    if (m_wireframeVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), m_wireframeVertexBufferMemory, nullptr);
    if (m_wireframeIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), m_wireframeIndexBuffer, nullptr);
    if (m_wireframeIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), m_wireframeIndexBufferMemory, nullptr);
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
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass; // Use the class member renderPass
    renderPassInfo.framebuffer = swapChainManager->getFramebuffer(imageIndex); // Get framebuffer from manager
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainManager->getExtent(); // Get extent from manager

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.39f, 0.58f, 0.93f, 1.0f}}; // Clear color for attachment 0
    clearValues[1].depthStencil = {1.0f, 0};             // Clear depth to 1.0 (farthest), stencil to 0 for attachment 1

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainManager->getExtent().width);
    viewport.height = static_cast<float>(swapChainManager->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // Set dynamic scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainManager->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline); // Bind the pipeline

    // Bind the main descriptor set (UBO + Atlas Texture) for the current frame
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &descriptorSetManager->getDescriptorSets()[currentFrame], 0, nullptr);

    // Iterate through chunk render data and draw each chunk
    for (const auto& pair : m_chunkRenderData) {
        const ChunkRenderData& chunkData = pair.second;
        if (chunkData.vertexBuffer != VK_NULL_HANDLE && chunkData.indexBuffer != VK_NULL_HANDLE && chunkData.indexCount > 0) {
            VkBuffer vertexBuffers[] = {chunkData.vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, chunkData.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            
            // Push model matrix for vertex shader
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, // offset
                sizeof(glm::mat4), // size
                &chunkData.modelMatrix
            );
            vkCmdDrawIndexed(commandBuffer, chunkData.indexCount, 1, 0, 0, 0); // firstIndex and vertexOffset are 0
        }
    }

    // --- Wireframe Rendering ---
    if (m_wireframeIndexCount > 0 && m_wireframeVertexBuffer != VK_NULL_HANDLE && m_wireframeIndexBuffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_wireframePipeline);
        // Descriptor set for UBO (view/proj) is already bound from main pass

        VkBuffer wireframeVertexBuffers[] = {m_wireframeVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, wireframeVertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_wireframeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(commandBuffer, m_wireframePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &m_wireframeModelMatrix);
        vkCmdDrawIndexed(commandBuffer, m_wireframeIndexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }


}

void VulkanRenderer::drawFrame() {
    vkWaitForFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // Process deletions for the frame whose fence just signaled
    for (const auto& resource : m_deletionQueues[currentFrame]) {
        // std::cout << "Deleting buffer: " << resource.buffer << " memory: " << resource.memory << " for frame " << currentFrame << std::endl;
        if (resource.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.buffer, nullptr);
        if (resource.memory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memory, nullptr);
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
            renderData.modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(relativeChunkCoord.x * CHUNK_WIDTH,
                                                                               relativeChunkCoord.y * CHUNK_HEIGHT,
                                                                               relativeChunkCoord.z * CHUNK_DEPTH));
        }
    }

    // Process chunk changes (mesh rebuilds for modified/new chunks, cleanup for unloaded)
    processChunkChanges();

    // Update the wireframe for the targeted block
    updateTargetedBlockWireframe();

    uint32_t imageIndex;
    VkResult result = swapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame], &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChainResources();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    // Mark the image as now being in use by this frame's fence
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    // Update uniform buffer for the current frame *before* recording command buffer
    updateUniformBuffer(currentFrame);

    // Only reset the fence if we are submitting commands to it.
    // This fence has been waited upon (top of drawFrame) and is now associated with imagesInFlight[imageIndex].
    // It needs to be reset before being signaled by vkQueueSubmit.
    vkResetFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    // Signal only the per-image presentation semaphore.
    // The inFlightFences[currentFrame] handles the frame-in-flight GPU completion.
    VkSemaphore signalSemaphores[] = {presentationFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1; 
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_vulkanDeviceRef.getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{}; // Get handle from manager
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    // Wait on the per-image presentation semaphore
    VkSemaphore presentWaitSemaphores[] = {presentationFinishedSemaphores[imageIndex]};
    presentInfo.pWaitSemaphores = presentWaitSemaphores;    VkSwapchainKHR swapChains[] = {swapChainManager->getSwapChainHandle()}; // Get handle from manager
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_vulkanDeviceRef.getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChainResources();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image!");
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
            if (resource.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), resource.buffer, nullptr);
            if (resource.memory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), resource.memory, nullptr);
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
        m_deletionQueues[currentFrame].push_back({data.vertexBuffer, data.vertexBufferMemory});
        // std::cout << "Queueing VB for deletion: " << data.vertexBuffer << " on frame " << currentFrame << std::endl;
        data.vertexBuffer = VK_NULL_HANDLE;
        data.vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (data.indexBuffer != VK_NULL_HANDLE) {
        m_deletionQueues[currentFrame].push_back({data.indexBuffer, data.indexBufferMemory});
        // std::cout << "Queueing IB for deletion: " << data.indexBuffer << " on frame " << currentFrame << std::endl;
        data.indexBuffer = VK_NULL_HANDLE;
        data.indexBufferMemory = VK_NULL_HANDLE;
    }
    data.indexCount = 0;
}

void VulkanRenderer::destroyAllChunkRenderData() {
    for (auto& pair : m_chunkRenderData) {
        destroyChunkRenderData(pair.second);
    }
    m_chunkRenderData.clear();
}

// This function now takes MeshData directly, to be called when an async task completes.
void VulkanRenderer::createChunkRenderDataFromMeshData(const glm::ivec3& chunkCoord, const ModelData& meshData) {
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
        bufferManager->createVertexBuffer(meshData.vertices, renderData.vertexBuffer, renderData.vertexBufferMemory);
        bufferManager->createIndexBuffer(meshData.indices, renderData.indexBuffer, renderData.indexBufferMemory);
        renderData.indexCount = static_cast<uint32_t>(meshData.indices.size());

        // Calculate the chunk's position relative to the current rebase origin
        glm::ivec3 rebaseOriginChunkCoord = m_world.getRebaseOriginChunkCoord();
        glm::ivec3 relativeChunkCoord = chunkCoord - rebaseOriginChunkCoord;
        glm::vec3 relativeWorldPos = glm::vec3(relativeChunkCoord.x * CHUNK_WIDTH,
                                               relativeChunkCoord.y * CHUNK_HEIGHT,
                                               relativeChunkCoord.z * CHUNK_DEPTH);
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

    // --- Stage 1: Check for completed mesh futures ---
    bool gpu_waited_this_frame_stage1 = false; // Track if we've waited for GPU idle in this stage
    // Iterate backwards to allow safe removal
    for (auto i = m_pendingMeshFutures.size(); i-- > 0;) {
        auto& future_entry = m_pendingMeshFutures[i];
        const glm::ivec3& future_chunk_coord = future_entry.first;
        std::future<ModelData>& future_obj = future_entry.second;

        // Check if the future is ready without blocking
        if (future_obj.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {                
                ModelData mesh_data_result = future_obj.get();
                // GPU buffer operations must happen on the main thread.
                // The vkDeviceWaitIdle here is removed.
                // VulkanBufferManager::createVertexBuffer/IndexBuffer use staging buffers
                // and internally wait on the graphics queue for the copy to complete,
                // which is less disruptive than waiting for the entire device.
                // if (!gpu_waited_this_frame_stage1) { // This logic is no longer needed
                //     vkDeviceWaitIdle(m_vulkanDeviceRef.getLogicalDevice());
                //     gpu_waited_this_frame_stage1 = true;
                // }
                //std::cout << "VulkanRenderer: APPLYING mesh for chunk [" << future_chunk_coord.x << "," << future_chunk_coord.y << "," << future_chunk_coord.z << "]." << std::endl;
                createChunkRenderDataFromMeshData(future_chunk_coord, mesh_data_result);
            } catch (const std::exception& e) {
                std::cerr << "Exception getting mesh future result for chunk [" << future_chunk_coord.x << "," << future_chunk_coord.y << "," << future_chunk_coord.z << "]: " << e.what() << std::endl;
            }
            // Task completed (successfully or with exception), remove from submitted set
            m_submittedMeshTasks.erase(future_chunk_coord);
            // Remove the processed future
            m_pendingMeshFutures.erase(m_pendingMeshFutures.begin() + i);
        }
    }

    // --- Stage 2: Identify chunks needing meshing and launch new async tasks ---
    const auto& changedChunks = m_world.getChangedChunks();
    std::set<glm::ivec3, IVec3Comparator> chunksToActuallyProcess = changedChunks; // Copy to iterate
    // Commented out the general "Changed chunks reported by World" as it's now conditional

    for (const glm::ivec3& chunkCoord : chunksToActuallyProcess) {
        Chunk* chunk = m_world.getChunk(chunkCoord); // Check if chunk still exists

        if (chunk) { // Chunk exists: modified or newly loaded, needs meshing
            if (m_pendingMeshFutures.size() < MAX_CONCURRENT_MESHING_TASKS) {
                // Only launch a new task if one isn't already submitted for this chunk
                if (m_submittedMeshTasks.find(chunkCoord) == m_submittedMeshTasks.end()) {
                    auto snapshot = chunk->getBlockDataSnapshot();
                    bool isAllAir = chunk->isAllAir(); // Get current all-air status
                    glm::ivec3 coordForTask = chunk->getChunkCoord(); // Get coord from chunk

                    // Launch async task. Pass world and resourceManager by const reference.
                    // std::cref ensures they are passed as references to the async task.
                    // Note: Accessing m_world from other threads needs careful consideration
                    // for thread safety if m_world's internal state (like m_chunks map)
                    // can be modified concurrently by the main thread. For now, we assume
                    // World::getBlockID and World::isChunkLoaded are sufficiently thread-safe for reads.
                    // The lambda captures necessary variables and returns ModelData.
                    auto mesh_future =
                        std::async(std::launch::async,
                            // Lambda function that returns ModelData
                            [ capturedCoord = coordForTask,          // Capture chunkCoord by value for the task
                              capturedSnapshot = std::move(snapshot), // Move chunk's own snapshot
                              capturedIsAllAir = isAllAir,           // Capture isAllAir by value
                              &world_ref = m_world,                  // Capture world by reference
                              &res_man_ref = *resourceManager,       // Capture resourceManager by reference
                              &block_reg_ref = m_blockRegistryRef    // Capture blockRegistry by reference
                            ]() mutable -> ModelData { // Changed return type to ModelData
                                ModelData meshDataResult = ChunkMesher::generateMesh(
                                    capturedCoord,
                                    std::move(capturedSnapshot), // Pass the moved snapshot
                                    capturedIsAllAir,
                                    world_ref,
                                    res_man_ref,
                                    block_reg_ref // Pass blockRegistry
                                );
                                // Return the pair
                                return meshDataResult; // No longer returning pair
                            }); // end of std::async
                        m_pendingMeshFutures.emplace_back(coordForTask, std::move(mesh_future));
                        m_world.acknowledgeChunkChangeProcessed(coordForTask); // Mark as processed by launching a task
                        m_submittedMeshTasks.insert(coordForTask); // Mark as submitted
                }
            }
        } else { // Chunk does not exist in world map: it was unloaded (or never existed but was in changed list)            
            // vkDeviceWaitIdle call removed here as destroyChunkRenderData now defers deletion.
            auto renderDataIt = m_chunkRenderData.find(chunkCoord);
            // If it was unloaded, it might have had a pending mesh task or existing render data.
            m_submittedMeshTasks.erase(chunkCoord); // Remove if it was in submitted set
            if (renderDataIt != m_chunkRenderData.end()) {
                destroyChunkRenderData(renderDataIt->second);
                m_chunkRenderData.erase(renderDataIt);
            }
            m_world.acknowledgeChunkChangeProcessed(chunkCoord); // Mark as processed (unloaded)
        }
    }
    // m_world.clearChangedChunks(); // REMOVED: Now handled by acknowledgeChunkChangeProcessed
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
                m_deletionQueues[currentFrame].push_back({m_wireframeVertexBuffer, m_wireframeVertexBufferMemory});
                m_wireframeVertexBuffer = VK_NULL_HANDLE;
                m_wireframeVertexBufferMemory = VK_NULL_HANDLE;
            }
            if (m_wireframeIndexBuffer != VK_NULL_HANDLE) {
                m_deletionQueues[currentFrame].push_back({m_wireframeIndexBuffer, m_wireframeIndexBufferMemory});
                m_wireframeIndexBuffer = VK_NULL_HANDLE;
                m_wireframeIndexBufferMemory = VK_NULL_HANDLE;
            }
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

    WireframeMesher::WireframeMeshData meshData = WireframeMesher::generateVoxelShapeMesh(shapeToMesh);

    // vkDeviceWaitIdle removed. BufferManager handles staging for creation.
    // Old buffers are queued for deferred deletion.

    // Destroy old buffers
    if (m_wireframeVertexBuffer != VK_NULL_HANDLE) {
        m_deletionQueues[currentFrame].push_back({m_wireframeVertexBuffer, m_wireframeVertexBufferMemory});
        m_wireframeVertexBuffer = VK_NULL_HANDLE;
        m_wireframeVertexBufferMemory = VK_NULL_HANDLE;
    }
    // m_wireframeVertexBufferMemory is handled with m_wireframeVertexBuffer

    if (m_wireframeIndexBuffer != VK_NULL_HANDLE) {
        m_deletionQueues[currentFrame].push_back({m_wireframeIndexBuffer, m_wireframeIndexBufferMemory});
        m_wireframeIndexBuffer = VK_NULL_HANDLE;
        m_wireframeIndexBufferMemory = VK_NULL_HANDLE;
    }
    // m_wireframeIndexBufferMemory is handled with m_wireframeIndexBuffer

    m_wireframeIndexCount = 0;

    if (!meshData.vertices.empty() && !meshData.indices.empty()) {
        bufferManager->createVertexBuffer(meshData.vertices, m_wireframeVertexBuffer, m_wireframeVertexBufferMemory);
        bufferManager->createIndexBuffer(meshData.indices, m_wireframeIndexBuffer, m_wireframeIndexBufferMemory);
        m_wireframeIndexCount = static_cast<uint32_t>(meshData.indices.size());
    }

    // Calculate model matrix
    glm::i64vec3 worldBlockPos_i64 = targetedBlockInfo.blockPosition;
    glm::ivec3 rebaseOriginChunkCoord_ivec3 = m_world.getRebaseOriginChunkCoord();
    glm::i64vec3 rebaseOriginWorldPos_i64 = glm::i64vec3(rebaseOriginChunkCoord_ivec3) * glm::i64vec3(CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH);
    glm::i64vec3 relativeBlockPos_i64 = worldBlockPos_i64 - rebaseOriginWorldPos_i64;
    m_wireframeModelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(relativeBlockPos_i64));
    
    m_wireframeMeshNeedsUpdate = false;
}