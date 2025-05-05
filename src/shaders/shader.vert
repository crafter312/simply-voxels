#version 450 // Specify GLSL version compatible with Vulkan

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor; // Let's add color per vertex

// Output to the fragment shader
layout(location = 0) out vec3 fragColor;

void main() {
    // For now, just pass the position through (no transformation yet)
    // We'll add MVP matrix multiplication here later
    gl_Position = vec4(inPosition, 1.0);

    // Pass the color to the fragment shader
    fragColor = inColor;
}