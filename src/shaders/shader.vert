#version 450 // Specify GLSL version compatible with Vulkan

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor; // Input color attribute


// Uniform Buffer Object containing transformation matrices
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// Output color to the fragment shader
layout(location = 0) out vec3 fragColor;

void main() {
    // Calculate final position in clip space using Model-View-Projection matrices
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // Pass the input color directly to the fragment shader
    fragColor = inColor;
}