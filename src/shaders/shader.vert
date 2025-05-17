#version 450 // Specify GLSL version compatible with Vulkan

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec3 inPosition; // Position attribute
layout(location = 1) in vec3 inNormal;   // Normal attribute
layout(location = 2) in vec2 inTexCoord; // Texture coordinates
layout(location = 3) in vec2 inAtlasUvOffset; // New input
layout(location = 4) in vec2 inAtlasUvScale;  // New input

// Uniform Buffer Object containing transformation matrices
// The model matrix is now handled by push constants
layout(binding = 0) uniform UniformBufferObject {
    // mat4 model; // Removed from UBO
    mat4 view;
    mat4 proj;
} ubo;

// Push constants for per-draw call data (like model matrix)
layout(push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

// Output texture coordinates to the fragment shader
layout(location = 0) out vec2 fragTexCoord; // Was location 1
layout(location = 1) out vec3 fragNormal;   // Output normal to fragment shader (world space)
layout(location = 2) out vec2 fragAtlasUvOffset; // New output
layout(location = 3) out vec2 fragAtlasUvScale;  // New output

void main() {
    // Calculate final position in clip space using Model-View-Projection matrices
    gl_Position = ubo.proj * ubo.view * pushConstants.model * vec4(inPosition, 1.0);
    // Pass texture coordinates to the fragment shader
    fragTexCoord = inTexCoord;
    // Transform normal to world space and pass to fragment shader
    // For non-uniform scaling, use mat3(transpose(inverse(pushConstants.model))) * inNormal
    fragNormal = normalize(mat3(pushConstants.model) * inNormal);

    fragAtlasUvOffset = inAtlasUvOffset; // Pass through atlas offset
    fragAtlasUvScale = inAtlasUvScale;   // Pass through atlas scale
}