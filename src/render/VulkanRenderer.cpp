#include "VulkanRenderer.hpp"
#include "VulkanSwapChain.hpp" // Include the new swap chain class
#include "VulkanPipelineFactory.hpp" // Include the new pipeline factory class
#include "VulkanBufferManager.hpp" // Include the new buffer manager class
#include "VulkanDevice.hpp"      // Include the VulkanDevice wrapper class definition
// #include "../resource/VulkanTextureLoader.hpp" // ResourceManager handles this
#include "../resource/ModelLoader.hpp"         // For ModelLoader::loadGltfModel and ModelData
#include "../resource/ResourceManager.hpp"     // Include the ResourceManager
#include "../world/ChunkMesher.hpp"            // For generating chunk meshes
#include "../world/World.hpp"                        // Include the World class definition
#include "../Camera.hpp"            // Include the Camera class definition

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
VulkanRenderer::VulkanRenderer(GLFWwindow* glfwWindow, VkInstance instance, VkSurfaceKHR surface, VulkanDevice& vulkanDevice, std::shared_ptr<Camera> cameraPtr)
    : window(glfwWindow),
      instanceRef(instance),
      surfaceRef(surface),
      m_vulkanDeviceRef(vulkanDevice), // Initialize the reference
      m_camera(cameraPtr) // m_blockRegistryRef initialization removed
{
    if (!window || instanceRef == VK_NULL_HANDLE || surfaceRef == VK_NULL_HANDLE ||
        // Access device properties through m_vulkanDeviceRef
        m_vulkanDeviceRef.getPhysicalDevice() == VK_NULL_HANDLE || m_vulkanDeviceRef.getLogicalDevice() == VK_NULL_HANDLE ||
        !m_vulkanDeviceRef.getQueueFamilyIndices().isComplete() || m_vulkanDeviceRef.getGraphicsQueue() == VK_NULL_HANDLE || m_vulkanDeviceRef.getPresentQueue() == VK_NULL_HANDLE ||
        !m_camera) // Check if camera pointer is valid
    {
        throw std::runtime_error("VulkanRenderer received null or invalid handles during construction!");
    }
    // Create the swap chain manager
    swapChainManager = std::make_unique<VulkanSwapChain>(instanceRef, m_vulkanDeviceRef.getPhysicalDevice(), m_vulkanDeviceRef.getLogicalDevice(), surfaceRef, window, m_vulkanDeviceRef.getQueueFamilyIndices());

    // The line below caused the error and is no longer needed as we use m_vulkanDeviceRef
    // m_vulkanDeviceWrapper = std::make_unique<VulkanDevice>(physicalDeviceRef, deviceRef); 
    // Create the descriptor set manager
    descriptorSetManager = std::make_unique<VulkanDescriptorSetManager>();

    std::cout << "VulkanRenderer constructed." << std::endl;
}

VulkanRenderer::~VulkanRenderer() {
    std::cout << "Cleaning up VulkanRenderer..." << std::endl;
    // Note: vkDeviceWaitIdle should be called before this destructor is invoked (e.g., in HelloVulkanApp::cleanup)

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

    // Descriptor pool and layout are now cleaned up by VulkanDescriptorSetManager's destructor
    // if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_vulkanDeviceRef.getLogicalDevice(), descriptorPool, nullptr);
    // if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_vulkanDeviceRef.getLogicalDevice(), descriptorSetLayout, nullptr);

    // Destroy aggregated chunk mesh buffers
    if (aggregatedVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), aggregatedVertexBuffer, nullptr);
    if (aggregatedVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), aggregatedVertexBufferMemory, nullptr);
    if (aggregatedIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_vulkanDeviceRef.getLogicalDevice(), aggregatedIndexBuffer, nullptr);
    if (aggregatedIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_vulkanDeviceRef.getLogicalDevice(), aggregatedIndexBufferMemory, nullptr);


    // Destroy synchronization objects
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (renderFinishedSemaphores.size() > i && renderFinishedSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), renderFinishedSemaphores[i], nullptr);
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_vulkanDeviceRef.getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_vulkanDeviceRef.getLogicalDevice(), inFlightFences[i], nullptr);
    }
    renderFinishedSemaphores.clear();
    imageAvailableSemaphores.clear();
    inFlightFences.clear();

    // Destroy command pool (also frees command buffers allocated from it)
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vulkanDeviceRef.getLogicalDevice(), commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    // Other resources (device, instance, surface) are managed by HelloVulkanApp
    std::cout << "VulkanRenderer cleanup complete." << std::endl;
}

void VulkanRenderer::init(const BlockRegistry& blockRegistryRef, const World& worldRef) {
    m_worldRef = &worldRef; // Store the reference to the world
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
    resourceManager->loadAssetsFromRegistry(blockRegistryRef, true); // Load assets using the passed BlockRegistry reference
    // Build the texture atlas. Assuming 16x16 tiles. This should ideally be configurable.
    const uint32_t ATLAS_TILE_SIZE = 32; // Manual coder note: changed from 16 to 32 to match the texture size
    resourceManager->buildTextureAtlas(blockRegistryRef, ATLAS_TILE_SIZE);
    std::cout << "Assets loaded by ResourceManager." << std::endl;
    // Create buffer manager now that command pool and graphics queue exist
    // This needs to be created before depth resources and render pass if render pass depends on depth format
    bufferManager = std::make_unique<VulkanBufferManager>(m_vulkanDeviceRef.getLogicalDevice(), m_vulkanDeviceRef.getPhysicalDevice(), commandPool, m_vulkanDeviceRef.getGraphicsQueue());

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
   
    pipelineFactory = std::make_unique<VulkanPipelineFactory>(m_vulkanDeviceRef.getLogicalDevice());
       if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", 
                                                   "shaders/frag.spv", 
                                                   descriptorSetManager->getDescriptorSetLayout(), 
                                                   renderPass, pipelineLayout, graphicsPipeline,
                                                   &pushConstantRange)) { // Pass push constant range
        throw std::runtime_error("Failed to create graphics pipeline using factory!");
    }
    std::cout << "Graphics Pipeline and Layout created." << std::endl;

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

    // --- Generate and aggregate chunk meshes ---
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    chunkDrawCommands.clear();

    uint32_t currentIndexBase = 0; // Renamed from currentIndexOffset to avoid confusion with VkDrawIndexedIndirectCommand
    int32_t currentVertexBase = 0;  // Renamed from currentVertexOffset

    if (m_worldRef) {
        // Assuming m_worldRef->getChunkMap() returns something like:
        // const std::map<ChunkPosition, std::unique_ptr<Chunk>>& getChunkMap() const;
        // (ChunkPosition would be a struct for map key)
        for (const auto& chunkPair : m_worldRef->getChunkMap()) {
            const Chunk& chunk = chunkPair.second; // chunkPair.second is already a Chunk object

            ChunkMesher::MeshData meshData = ChunkMesher::generateMesh(chunk, *m_worldRef, *resourceManager);

            if (meshData.indices.empty() || meshData.vertices.empty()) {
                continue; // Skip empty meshes
            }

            ChunkDrawCommand cmd;
            cmd.indexCount = static_cast<uint32_t>(meshData.indices.size());
            cmd.firstIndex = currentIndexBase;
            cmd.vertexOffset = currentVertexBase;
            
            glm::vec3 chunkWorldPosFloat = glm::vec3(chunk.getWorldPosition());
            cmd.modelMatrix = glm::translate(glm::mat4(1.0f), chunkWorldPosFloat);
            
            chunkDrawCommands.push_back(cmd);

            // Append vertices
            allVertices.insert(allVertices.end(), meshData.vertices.begin(), meshData.vertices.end());
            
            // Append indices, adjusting them for the global vertex offset
            for (uint32_t index : meshData.indices) {
                allIndices.push_back(index + currentVertexBase);
            }
            
            currentIndexBase += cmd.indexCount;
            currentVertexBase += static_cast<int32_t>(meshData.vertices.size());
        }
    }
    totalAggregatedIndices = static_cast<uint32_t>(allIndices.size());

    if (!allVertices.empty() && !allIndices.empty()) {
        bufferManager->createVertexBuffer(allVertices, aggregatedVertexBuffer, aggregatedVertexBufferMemory);
        std::cout << "Aggregated Vertex Buffer created. Vertices: " << allVertices.size() << std::endl;
        bufferManager->createIndexBuffer(allIndices, aggregatedIndexBuffer, aggregatedIndexBufferMemory);
        std::cout << "Aggregated Index Buffer created. Indices: " << allIndices.size() << std::endl;
    } else {
        std::cout << "No chunk mesh data to create buffers from. Renderer will draw nothing." << std::endl;
    }

    createCommandBuffers();
    std::cout << "Command Buffers created." << std::endl;
    createSyncObjects();
    std::cout << "Synchronization Objects created." << std::endl;
    std::cout << "VulkanRenderer initialization complete." << std::endl;
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
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(m_vulkanDeviceRef.getLogicalDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_vulkanDeviceRef.getLogicalDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_vulkanDeviceRef.getLogicalDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }
}

// --- Graphics Pipeline Creation ---

// --- Buffer Creation ---

// --- Update Uniform Buffer ---

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};
    // Get view and projection matrices from the camera
    ubo.view = m_camera->getViewMatrix();
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

    // Bind the aggregated vertex and index buffers if they exist and contain data
    if (aggregatedVertexBuffer != VK_NULL_HANDLE && aggregatedIndexBuffer != VK_NULL_HANDLE && totalAggregatedIndices > 0) {
        VkBuffer vertexBuffers[] = {aggregatedVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, aggregatedIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Bind the main descriptor set (UBO + Atlas Texture) for the current frame
        // descriptorSetManager->getDescriptorSets() returns a vector indexed by frame-in-flight.
        // 'currentFrame' (class member) is the correct index for UBOs and their descriptor sets.
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSetManager->getDescriptorSets()[currentFrame], 0, nullptr);

        // Loop through pre-calculated chunk draw commands
        for (const auto& cmd : chunkDrawCommands) {
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, // offset
                sizeof(glm::mat4), // size
                &cmd.modelMatrix); // Use pre-calculated model matrix for the chunk

            vkCmdDrawIndexed(commandBuffer, cmd.indexCount, 1, cmd.firstIndex, cmd.vertexOffset, 0);
        }
    } else {
        // Optionally, log that nothing is being drawn if buffers are not ready
        // std::cout << "RecordCommandBuffer: No aggregated mesh data to draw." << std::endl;
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }


}

void VulkanRenderer::drawFrame() {
    vkWaitForFences(m_vulkanDeviceRef.getLogicalDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = swapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame], &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChainResources(); // Call the renamed function
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    // Update uniform buffer for the current frame *before* recording command buffer
    updateUniformBuffer(currentFrame);

    // prepareBlockTextures call removed as atlas is static and bound once per frame via descriptor set

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
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    if (vkQueueSubmit(m_vulkanDeviceRef.getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{}; // Get handle from manager
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapChainManager->getSwapChainHandle()}; // Get handle from manager
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_vulkanDeviceRef.getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChainResources(); // Call the renamed function
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

    // 2. Cleanup old depth buffer resources (as they depend on extent)
    cleanupDepthResources();

    // 3. Cleanup renderer resources dependent on the swap chain/render pass
    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vulkanDeviceRef.getLogicalDevice(), graphicsPipeline, nullptr); // Nullify handles after destruction
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vulkanDeviceRef.getLogicalDevice(), pipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_vulkanDeviceRef.getLogicalDevice(), renderPass, nullptr);
    graphicsPipeline = VK_NULL_HANDLE; // Nullify handles after destruction
    pipelineLayout = VK_NULL_HANDLE;
    renderPass = VK_NULL_HANDLE;

    // 4. Recreate swap chain and image views
    swapChainManager->createSwapChainInternal(); // Creates swap chain, gets new format/extent
    swapChainManager->createImageViews();
    std::cout << "Swap chain and image views recreated by manager." << std::endl;

    // 5. Recreate depth buffer resources (depends on new swap chain extent)
    bufferManager->createDepthResources(swapChainManager->getExtent(), depthImage, depthImageMemory, depthImageView, depthFormat);
    std::cout << "Depth Resources recreated." << std::endl;

    // 6. Recreate render pass (depends on new format and new depth format)
    createRenderPass();
    std::cout << "Render pass recreated." << std::endl;

    // 7. Recreate graphics pipeline (depends on new render pass)
    // Define the push constant range again, as it's needed for pipeline recreation
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);

    if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", "shaders/frag.spv", descriptorSetManager->getDescriptorSetLayout(), renderPass, pipelineLayout, graphicsPipeline, &pushConstantRange)) {
        throw std::runtime_error("Failed to recreate graphics pipeline using factory!");
    }
    std::cout << "Graphics pipeline recreated." << std::endl;

    // 8. Recreate framebuffers (depends on new image views, render pass, and new depth image view)
    swapChainManager->createFramebuffers(renderPass, depthImageView);

    // Command buffers need to be re-recorded because they reference the old framebuffers.
    // We don't explicitly recreate them here because the drawFrame loop resets and
    // re-records the command buffer for the current frame anyway.
    std::cout << "Swap chain dependent resources fully recreated." << std::endl;
}