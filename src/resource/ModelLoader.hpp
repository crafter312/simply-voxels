#ifndef MODELLOADER_HPP
#define MODELLOADER_HPP

#include <vulkan/vulkan.h> // For VkVertexInputBindingDescription, VkVertexInputAttributeDescription, VK_FORMAT_*, VK_VERTEX_INPUT_RATE_VERTEX

#define GLM_FORCE_RADIANS // Ensure GLM uses radians
#include <glm/glm.hpp>    // For vec3

#include <string>
#include <vector>
#include <array>    // For std::array
#include <cstddef>  // For offsetof
#include <cstdint> // For uint32_t

// Moved Vertex struct here
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;   // Normal vector
    glm::vec2 texCoord; // Texture coordinates (UVs)

    // Describe how to pass this vertex data to the vertex shader
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // Index of the binding in the array of bindings
        bindingDescription.stride = sizeof(Vertex); // Distance in bytes between consecutive elements
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to the next data entry after each vertex
        return bindingDescription;
    }

    // Describe how to handle vertex input attributes
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescription() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        // Position attribute: location 0, binding 0
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Normal attribute: location 1, binding 0
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Texture Coordinate attribute: location 2, binding 0
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

// Structure to hold loaded model data
struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // Using uint32_t for indices from GLTF
};

namespace ModelLoader {

/**
 * Loads a 3D model from a GLTF (.glb) file.
 * @param filepath Path to the .glb file.
 * @param outModelData Structure to populate with vertex and index data.
 * @return True if loading was successful, false otherwise.
 */
bool loadGltfModel(const std::string& filepath, ModelData& outModelData);

} // namespace ModelLoader

#endif // MODELLOADER_HPP