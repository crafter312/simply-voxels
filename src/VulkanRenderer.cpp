#include "VulkanRenderer.hpp"
#include "VulkanSwapChain.hpp" // Include the new swap chain class
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
    createGraphicsPipeline(); // Create pipeline after render pass
    std::cout << "Graphics Pipeline created." << std::endl;
    swapChainManager->createFramebuffers(renderPass); // Create framebuffers (needs render pass and image views)
    std::cout << "Framebuffers created." << std::endl;
    createCommandPool();
    std::cout << "Command Pool created." << std::endl;
    // Create UBO resources
    createUniformBuffers();
    std::cout << "Uniform Buffers created." << std::endl;
    createDescriptorPool();
    std::cout << "Descriptor Pool created." << std::endl;
    createDescriptorSets();
    std::cout << "Descriptor Sets created." << std::endl;
    // Create buffers *after* command pool (needed for transfer commands)
    createVertexBuffer();
    std::cout << "Vertex Buffer created." << std::endl;
    createIndexBuffer();
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

// Static helper function to read shader files
std::vector<char> VulkanRenderer::readFile(const std::string& filename) {
    // Start reading from the end of the file (ate) in binary mode (binary)
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Get the file size from the read position (which is at the end)
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    // Seek back to the beginning of the file
    file.seekg(0);
    // Read the entire file into the buffer
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    // Note: The pointer needs to be uint32_t*, so we cast
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(deviceRef, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    return shaderModule;
}

void VulkanRenderer::createGraphicsPipeline() {
    // --- Load Shader Code ---
    // Assuming shaders are in a 'shaders' directory relative to execution
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

    // --- Create Shader Modules ---
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    // --- Shader Stage Creation ---
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main"; // Entry point function name in shader

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // --- Vertex Input ---
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescription(); // Get the array

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()); // Use the size of the array
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data(); // Pass pointer to the array data

    // --- Input Assembly --- (Triangles from vertices)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // --- Viewport and Scissor --- (Dynamic state, but need placeholder here)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // --- Rasterizer --- (Turns primitives into fragments)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE; // Keep fragments
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // Fill the triangles
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // Cull back faces
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // Define front face (adjust if needed based on vertex order/winding)
    rasterizer.depthBiasEnable = VK_FALSE;

    // --- Multisampling --- (Disabled for now)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- Color Blending --- (Basic pass-through)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE; // No blending for solid color

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // --- Dynamic States --- (Viewport and Scissor can be changed without recreating pipeline)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // --- Pipeline Layout --- (Defines uniforms/push constants)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1; // We have one descriptor set layout
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; // Point to the layout we created
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(deviceRef, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    // --- Graphics Pipeline Creation ---
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // No depth testing (yet)
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass; // The render pass this pipeline is compatible with
    pipelineInfo.subpass = 0; // Index of the subpass where this pipeline will be used
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional: For deriving from another pipeline
    pipelineInfo.basePipelineIndex = -1; // Optional

    if (vkCreateGraphicsPipelines(deviceRef, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }

    // --- Cleanup Shader Modules --- (No longer needed after pipeline creation)
    vkDestroyShaderModule(deviceRef, fragShaderModule, nullptr);
    vkDestroyShaderModule(deviceRef, vertShaderModule, nullptr);
}

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

void VulkanRenderer::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT); // Resize mapped pointers vector

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(bufferSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     uniformBuffers[i],
                     uniformBuffersMemory[i]);

        // Persistently map the buffer
        vkMapMemory(deviceRef, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
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

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDeviceRef, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        // Check if the memory type index 'i' is suitable (matches typeFilter bit)
        // AND if it has all the required property flags
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Only used by graphics queue

    if (vkCreateBuffer(deviceRef, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(deviceRef, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(deviceRef, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    // Associate the memory with the buffer
    vkBindBufferMemory(deviceRef, buffer, bufferMemory, 0); // 0 is the offset
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool; // Use the class's command pool
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(deviceRef, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // We're only using it once

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Submit to the graphics queue and wait for it to finish
    vkQueueSubmit(graphicsQueueRef, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueueRef); // Wait for the transfer to complete

    // Clean up the temporary command buffer
    vkFreeCommandBuffers(deviceRef, commandPool, 1, &commandBuffer);
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(cubeVertices[0]) * cubeVertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(deviceRef, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, cubeVertices.data(), (size_t)bufferSize);
    vkUnmapMemory(deviceRef, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(deviceRef, stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, stagingBufferMemory, nullptr);
}

void VulkanRenderer::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(cubeIndices[0]) * cubeIndices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(deviceRef, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, cubeIndices.data(), (size_t)bufferSize);
    vkUnmapMemory(deviceRef, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(deviceRef, stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, stagingBufferMemory, nullptr);
}

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

    // 4. Recreate render pass (depends on new format)
    createRenderPass();

    // 5. Recreate graphics pipeline (depends on new render pass)
    createGraphicsPipeline();

    // 6. Recreate framebuffers (depends on new image views and render pass)
    swapChainManager->createFramebuffers(renderPass);

    // Command buffers need to be re-recorded because they reference the old framebuffers.
    // We don't explicitly recreate them here because the drawFrame loop resets and
    // re-records the command buffer for the current frame anyway.
    std::cout << "Swap chain recreated." << std::endl;
}