#version 450 // Specify GLSL version compatible with Vulkan

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec3 inPosition;
// We don't need per-vertex color for a solid cube

// Uniform Buffer Object containing transformation matrices
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    // Calculate final position in clip space using Model-View-Projection matrices
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
}