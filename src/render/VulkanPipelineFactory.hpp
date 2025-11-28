#pragma once

#include "VulkanCommon.hpp" // Common Vulkan includes and definitions

#include <glm/glm.hpp> // For glm::mat4 in push constants, though not directly used in this header
#include <string>
#include <vector>
#include "../resource/ModelLoader.hpp" // For Vertex::getBindingDescription and Vertex::getAttributeDescription (Vertex is now in ModelLoader.hpp)

// Forward declare VulkanDevice
class VulkanDevice;

class VulkanPipelineFactory {
public:
    VulkanPipelineFactory(VulkanDevice& vulkanDevice);
    ~VulkanPipelineFactory(); // Though likely empty if it doesn't own Vulkan resources directly

    // Creates the graphics pipeline and its layout
    // Returns true on success, false on failure
    bool createGraphicsPipeline(
        const std::string& vertShaderPath,
        const std::string& fragShaderPath,
        VkDescriptorSetLayout descriptorSetLayout, // Input: layout for UBOs, etc.
        VkRenderPass renderPass,                   // Input: compatible render pass
        VkPipelineLayout& outPipelineLayout,       // Output: created pipeline layout
        VkPipeline& outGraphicsPipeline,           // Output: created graphics pipeline
        const VkPushConstantRange* pushConstantRange = nullptr // Optional: For push constants
    );

    // Creates the wireframe rendering pipeline and its layout
    // Returns true on success, false on failure
    bool createWireframePipeline(
        const std::string& vertShaderPath,         // Path to wireframe vertex shader
        const std::string& fragShaderPath,         // Path to wireframe fragment shader
        VkDescriptorSetLayout descriptorSetLayout, // Input: layout for UBOs (view/proj)
        VkRenderPass renderPass,                   // Input: compatible render pass
        VkPipelineLayout& outPipelineLayout,       // Output: created pipeline layout for wireframe
        VkPipeline& outWireframePipeline,          // Output: created wireframe graphics pipeline
        const VkPushConstantRange* pushConstantRange = nullptr // Optional: For model matrix push constants
    );


private:
    VulkanDevice& m_vulkanDeviceRef; // Store a reference to the VulkanDevice

    // Helper methods, moved from VulkanRenderer
    static std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
};