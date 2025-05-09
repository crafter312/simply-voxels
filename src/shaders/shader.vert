#version 450 // Specify GLSL version compatible with Vulkan

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec3 inPosition; // Position attribute
layout(location = 1) in vec2 inTexCoord; // Texture coordinates (was location 2)

// Uniform Buffer Object containing transformation matrices
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// Output texture coordinates to the fragment shader
layout(location = 0) out vec2 fragTexCoord; // Was location 1

void main() {
    // Calculate final position in clip space using Model-View-Projection matrices
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // Pass texture coordinates to the fragment shader
    fragTexCoord = inTexCoord;
}