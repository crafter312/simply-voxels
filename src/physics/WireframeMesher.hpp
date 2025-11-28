 #ifndef PHYSICS_WIREFRAMEMESHER_HPP
 #define PHYSICS_WIREFRAMEMESHER_HPP
 
#include "../render/VulkanCommon.hpp" // For VkVertexInputBindingDescription, VkVertexInputAttributeDescription, VK_FORMAT_*, VK_VERTEX_INPUT_RATE_VERTEX

 #include <glm/glm.hpp>
 #include <vector>
 #include <array>
 #include <cstddef> // For offsetof
 #include <cstdint> // For uint32_t
 
 // Forward declare VoxelShape
 namespace Physics { class VoxelShape; }
 
 // Define a constexpr for the wireframe color
 constexpr glm::vec3 BLACK_COLOR(0.0f, 0.0f, 0.0f);

 namespace WireframeMesher {
 
 // Structure for a wireframe vertex (only position needed)
 struct WireframeVertex {
     glm::vec3 pos;
     glm::vec3 color; // Added color
 
     // Describe how to pass this vertex data to the vertex shader
     static VkVertexInputBindingDescription getBindingDescription() {
         VkVertexInputBindingDescription bindingDescription{};
         bindingDescription.binding = 0; // Index of the binding in the array of bindings
         bindingDescription.stride = sizeof(WireframeVertex); // Distance in bytes between consecutive elements
         bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to the next data entry after each vertex
         return bindingDescription;
     }
 
     // Describe how to handle vertex input attributes
     static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription() {
         std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
 
         // Position attribute: location 0, binding 0
         attributeDescriptions[0].location = 0;
         attributeDescriptions[0].binding = 0;
         attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3
         attributeDescriptions[0].offset = offsetof(WireframeVertex, pos);
 
         // Color attribute: location 1, binding 0
         attributeDescriptions[1].location = 1;
         attributeDescriptions[1].binding = 0;
         attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3 for color
         attributeDescriptions[1].offset = offsetof(WireframeVertex, color);
 
 
         return attributeDescriptions;
     }
 };
 
 // Structure to hold generated wireframe mesh data
 struct WireframeMeshData {
     std::vector<WireframeVertex> vertices;
     std::vector<uint32_t> indices; // Using uint32_t for indices
 };
 
 /**
  * @brief Generates wireframe mesh data (vertices and indices for line lists)
  *        for a given VoxelShape.
  * 
  * @param voxelShape The VoxelShape containing one or more AABBs.
  * @return WireframeMeshData containing the vertices and indices for drawing the wireframe.
  */
 WireframeMeshData generateVoxelShapeMesh(const Physics::VoxelShape& voxelShape);
 
 } // namespace WireframeMesher
 
 #endif // PHYSICS_WIREFRAMEMESHER_HPP