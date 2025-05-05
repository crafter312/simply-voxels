#version 450 // Specify GLSL version compatible with Vulkan

// We don't need input from the vertex shader for a solid color

// Output color for the current pixel
layout(location = 0) out vec4 outColor;

void main() {
    // Output a solid color (e.g., a nice orange)
    outColor = vec4(1.0, 0.5, 0.2, 1.0);
}