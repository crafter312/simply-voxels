#ifndef VERTEX_HPP
#define VERTEX_HPP

#include <vulkan/vulkan.h> // For VkVertexInputBindingDescription, VkVertexInputAttributeDescription, VK_FORMAT_*, VK_VERTEX_INPUT_RATE_VERTEX

#define GLM_FORCE_RADIANS // Ensure GLM uses radians
#include <glm/glm.hpp>    // For vec3

#include <vector>
#include <array>    // For std::array
#include <cstddef>  // For offsetof
#include <cstdint>  // For uint16_t

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color; // Add color attribute

    // Describe how to pass this vertex data to the vertex shader
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0; // Index of the binding in the array of bindings
        bindingDescription.stride = sizeof(Vertex); // Distance in bytes between consecutive elements
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to the next data entry after each vertex

        return bindingDescription;
    }

    // Describe how to handle vertex input attribute (position)
    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescription() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        // Position attribute
        attributeDescriptions[0].binding = 0; // Which binding the per-vertex data comes from
        attributeDescriptions[0].location = 0; // Corresponds to 'location = 0' in the vertex shader
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // Format of the data (vec3)
        attributeDescriptions[0].offset = offsetof(Vertex, pos); // Byte offset of this attribute within the struct

        // Color attribute
        attributeDescriptions[1].binding = 0; // Data comes from the same binding
        attributeDescriptions[1].location = 1; // Corresponds to 'location = 1' in the vertex shader
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // Format of the data (vec3)
        attributeDescriptions[1].offset = offsetof(Vertex, color); // Byte offset of this attribute

        return attributeDescriptions;
    }
};

// Define the vertices and indices for a cube
// We'll define these as constants directly in the header for simplicity,
// though for larger models, you'd load them from a file.
const std::vector<Vertex> cubeVertices = {
    // Front face (Z+) Red
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}}, // 3
    // Back face (Z-) Cyan
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 4
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 5
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 6
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}}, // 7
    // Left face (X-) Green
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}}, // 8 (orig 4)
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 9 (orig 0)
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}}, // 10 (orig 3)
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}}, // 11 (orig 7)
    // Right face (X+) Magenta
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}}, // 12 (orig 1)
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 13 (orig 5)
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}}, // 14 (orig 6)
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}}, // 15 (orig 2)
    // Top face (Y+) Blue
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}, // 16 (orig 3)
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}, // 17 (orig 2)
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}}, // 18 (orig 6)
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}}, // 19 (orig 7)
    // Bottom face (Y-) Yellow
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}}, // 20 (orig 4)
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}}, // 21 (orig 5)
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}}, // 22 (orig 1)
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}}  // 23 (orig 0)
};

// Indices define the triangles that make up the cube faces
// Using uint16_t is fine for simple models like a cube (< 65536 vertices)
// Now indices reference the duplicated vertices above. 6 faces * 2 triangles/face * 3 vertices/triangle = 36 indices
const std::vector<uint16_t> cubeIndices = {
    // Triangles are now defined with a Clockwise (CW) winding order
    // when viewed from the outside of the cube. This is because the
    // projection matrix flips the Y-coordinate (proj[1][1] *= -1),
    // which inverts the winding order. So, CW in model space becomes
    // CCW in screen space, which is what Vulkan's default front_face expects.

    // Front face (Z+) Red - Original CCW: 0,1,2,  2,3,0
     0,  2,  1,   2,  0,  3,
    // Back face (Z-) Cyan - Original CCW: 4,5,6,  6,7,4
     4,  5,  6,   6,  7,  4, // Changed to CCW
    // Left face (X-) Green - Original CCW: 8,9,10, 10,11,8
     8, 10,  9,  10,  8, 11,
    // Right face (X+) Magenta - Original CCW: 12,13,14, 14,15,12
    12, 14, 13,  14, 12, 15,
    // Top face (Y+) Blue - Original CCW: 16,17,18, 18,19,16
    16, 18, 17,  18, 16, 19,
    // Bottom face (Y-) Yellow - Original CCW: 20,21,22, 22,23,20
    20, 22, 21,  22, 20, 23
};

#endif // VERTEX_HPP