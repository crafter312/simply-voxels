#version 450 // Specify GLSL version compatible with Vulkan

// Input from the vertex shader (interpolated)
layout(location = 0) in vec3 fragColor;

// Output color for the current pixel
layout(location = 0) out vec4 outColor;

void main() {
    // Output the interpolated color received from the vertex shader
    outColor = vec4(fragColor, 1.0);
}