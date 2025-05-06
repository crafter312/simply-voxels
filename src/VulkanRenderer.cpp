#include "VulkanRenderer.hpp"
#include "VulkanSwapChain.hpp" // Include the new swap chain class
#include "VulkanPipelineFactory.hpp" // Include the new pipeline factory class
#include "VulkanBufferManager.hpp" // Include the new buffer manager class
#include "HelloVulkanApp.hpp" // Include for QueueFamilyIndices definition

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

VulkanRenderer::VulkanRenderer(GLFWwindow* glfwWindow, VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice logicalDevice, QueueFamilyIndices queueIndices, VkQueue graphicsQueueHandle, VkQueue presentQueueHandle)
    : window(glfwWindow),
      instanceRef(instance),
      surfaceRef(surface),
      physicalDeviceRef(physicalDevice),
      deviceRef(logicalDevice),
      graphicsQueueRef(graphicsQueueHandle),
      presentQueueRef(presentQueueHandle)
{
    // Create a shared instance of QueueFamilyIndices
    queueIndicesRef = std::make_shared<QueueFamilyIndices>(queueIndices);
    if (!window || instanceRef == VK_NULL_HANDLE || surfaceRef == VK_NULL_HANDLE ||
        physicalDeviceRef == VK_NULL_HANDLE || deviceRef == VK_NULL_HANDLE ||
        !queueIndicesRef->isComplete() || graphicsQueueRef == VK_NULL_HANDLE || presentQueueRef == VK_NULL_HANDLE)
    {
        throw std::runtime_error("VulkanRenderer received null or invalid handles during construction!");
    }
    // Create the swap chain manager
    // Pass the shared pointer to the swap chain manager
    swapChainManager = std::make_unique<VulkanSwapChain>(instanceRef, physicalDeviceRef, deviceRef, surfaceRef, window, queueIndicesRef);
    std::cout << "VulkanRenderer constructed." << std::endl;
}

VulkanRenderer::~VulkanRenderer() {
    std::cout << "Cleaning up VulkanRenderer..." << std::endl;
    // Note: vkDeviceWaitIdle should be called before this destructor is invoked (e.g., in HelloVulkanApp::cleanup)

    // Swap chain resources are cleaned up by swapChainManager's destructor
    swapChainManager.reset(); // Explicitly reset before other resources if needed, though RAII handles it

    // Destroy graphics pipeline and layout
    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(deviceRef, graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(deviceRef, pipelineLayout, nullptr);

    // Destroy render pass
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(deviceRef, renderPass, nullptr);

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffersMapped[i] != nullptr) { // Check if mapped
            vkUnmapMemory(deviceRef, uniformBuffersMemory[i]); // Unmap before freeing memory
            // uniformBuffersMapped[i] = nullptr; // Optional: vector will be cleared or resized
        }
        if (uniformBuffers[i] != VK_NULL_HANDLE) vkDestroyBuffer(deviceRef, uniformBuffers[i], nullptr);
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) vkFreeMemory(deviceRef, uniformBuffersMemory[i], nullptr);
    }

    // Destroy descriptor pool (frees descriptor sets automatically)
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(deviceRef, descriptorPool, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(deviceRef, descriptorSetLayout, nullptr);

    // Destroy vertex buffer
    if (vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(deviceRef, vertexBuffer, nullptr);
    if (vertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(deviceRef, vertexBufferMemory, nullptr);

    // Destroy index buffer
    if (indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(deviceRef, indexBuffer, nullptr);
    if (indexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(deviceRef, indexBufferMemory, nullptr);

    // Destroy synchronization objects
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (renderFinishedSemaphores.size() > i && renderFinishedSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(deviceRef, renderFinishedSemaphores[i], nullptr);
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(deviceRef, imageAvailableSemaphores[i], nullptr);
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(deviceRef, inFlightFences[i], nullptr);
    }
    renderFinishedSemaphores.clear();
    imageAvailableSemaphores.clear();
    inFlightFences.clear();

    // Destroy command pool (also frees command buffers allocated from it)
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(deviceRef, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    // Other resources (device, instance, surface) are managed by HelloVulkanApp
    std::cout << "VulkanRenderer cleanup complete." << std::endl;
}

void VulkanRenderer::init() {
    std::cout << "Initializing VulkanRenderer..." << std::endl;
    // Initialize swap chain (creates chain and image views)
    swapChainManager->init();
    std::cout << "Swap Chain initialized." << std::endl;
    // Create render pass (needs swap chain format)
    createRenderPass();
    std::cout << "Render Pass created." << std::endl;
    // Create descriptor layout before pipeline layout
    createDescriptorSetLayout();

    // Create and use the pipeline factory
    pipelineFactory = std::make_unique<VulkanPipelineFactory>(deviceRef);
    if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", "shaders/frag.spv", descriptorSetLayout, renderPass, pipelineLayout, graphicsPipeline)) {
        throw std::runtime_error("Failed to create graphics pipeline using factory!");
    }
    std::cout << "Graphics Pipeline and Layout created." << std::endl;

    swapChainManager->createFramebuffers(renderPass); // Create framebuffers (needs render pass and image views)
    createCommandPool();
    std::cout << "Command Pool created." << std::endl;

    // Create buffer manager now that command pool and graphics queue exist
    bufferManager = std::make_unique<VulkanBufferManager>(deviceRef, physicalDeviceRef, commandPool, graphicsQueueRef);

    // Create UBO resources
    bufferManager->createUniformBuffers(MAX_FRAMES_IN_FLIGHT, sizeof(UniformBufferObject), uniformBuffers, uniformBuffersMemory, uniformBuffersMapped);
    std::cout << "Uniform Buffers created." << std::endl;
    createDescriptorPool();
    std::cout << "Descriptor Pool created." << std::endl;
    createDescriptorSets();
    std::cout << "Descriptor Sets created." << std::endl;
    // Create buffers *after* command pool (needed for transfer commands)
    bufferManager->createVertexBuffer(cubeVertices, vertexBuffer, vertexBufferMemory);
    std::cout << "Vertex Buffer created." << std::endl;
    bufferManager->createIndexBuffer(cubeIndices, indexBuffer, indexBufferMemory);
    std::cout << "Index Buffer created." << std::endl;
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

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(deviceRef, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueIndicesRef->graphicsFamily.value();

    if (vkCreateCommandPool(deviceRef, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
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

    if (vkAllocateCommandBuffers(deviceRef, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
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
        if (vkCreateSemaphore(deviceRef, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(deviceRef, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(deviceRef, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }
}

// --- Graphics Pipeline Creation ---

// --- Descriptor Set Layout, Pool, Sets, and Uniform Buffers ---

void VulkanRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0; // Corresponds to "binding = 0" in the shader
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1; // We have one UBO
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // UBO is used in the vertex shader
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(deviceRef, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void VulkanRenderer::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT); // Enough descriptors for our UBOs

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT); // Max number of sets we can allocate

    if (vkCreateDescriptorPool(deviceRef, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void VulkanRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(deviceRef, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }

    // Configure the descriptor sets to point to our buffers
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0; // Matches the layout binding
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(deviceRef, 1, &descriptorWrite, 0, nullptr);
    }
}

// --- Buffer Creation ---

// --- Update Uniform Buffer ---

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};

    // Model: Place the cube at the origin (identity matrix)
    ubo.model = glm::mat4(1.0f); // Could add rotation here later if desired

    // View: Look at the cube from +3 on the Z axis
    ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), // Camera position
                           glm::vec3(0.0f, 0.0f, 0.0f), // Target position (center of cube)
                           glm::vec3(0.0f, 1.0f, 0.0f)); // Up vector (Y is up)

    // Projection: 45 degree field of view, perspective
    ubo.proj = glm::perspective(glm::radians(45.0f), swapChainManager->getExtent().width / (float) swapChainManager->getExtent().height, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1; // Invert Y axis for Vulkan clip space (GLM default is OpenGL style)

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

    VkClearValue clearColor = {{{0.39f, 0.58f, 0.93f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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

    // Bind the descriptor set for the current frame
    vkCmdBindDescriptorSets(commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, // The layout the descriptors are based on
        0, 1, &descriptorSets[currentFrame], // Set index 0, 1 set, pointer to the set
        0, nullptr); // Dynamic offsets (none)

    // Bind the vertex buffer
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets); // Binding 0, 1 buffer, starting at offset 0

    // Bind the index buffer (using uint16_t indices)
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // Draw indexed command
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

void VulkanRenderer::drawFrame() {
    vkWaitForFences(deviceRef, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

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

    vkResetFences(deviceRef, 1, &inFlightFences[currentFrame]);

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

    if (vkQueueSubmit(graphicsQueueRef, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapChainManager->getSwapChainHandle()}; // Get handle from manager
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueueRef, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChainResources(); // Call the renamed function
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::recreateSwapChainResources() {
    std::cout << "Recreating swap chain dependent resources..." << std::endl;

    // Wait for the device to be idle before cleanup/recreation
    vkDeviceWaitIdle(deviceRef);

    // 1. Cleanup old swap chain resources (swap chain, image views, framebuffers)
    swapChainManager->cleanupForRecreation();

    // 2. Cleanup renderer resources dependent on the swap chain/render pass
    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(deviceRef, graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(deviceRef, pipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(deviceRef, renderPass, nullptr);

    // 3. Recreate swap chain and image views
    swapChainManager->createSwapChainInternal(); // Creates swap chain, gets new format/extent
    swapChainManager->createImageViews();
    std::cout << "Swap chain and image views recreated by manager." << std::endl;

    // 4. Recreate render pass (depends on new format)
    createRenderPass();
    std::cout << "Render pass recreated." << std::endl;

    // 5. Recreate graphics pipeline (depends on new render pass)
    if (!pipelineFactory->createGraphicsPipeline("shaders/vert.spv", "shaders/frag.spv", descriptorSetLayout, renderPass, pipelineLayout, graphicsPipeline)) {
        throw std::runtime_error("Failed to recreate graphics pipeline using factory!");
    }
    std::cout << "Graphics pipeline recreated." << std::endl;

    // 6. Recreate framebuffers (depends on new image views and render pass)
    swapChainManager->createFramebuffers(renderPass);

    // Command buffers need to be re-recorded because they reference the old framebuffers.
    // We don't explicitly recreate them here because the drawFrame loop resets and
    // re-records the command buffer for the current frame anyway.
    std::cout << "Swap chain dependent resources fully recreated." << std::endl;
}