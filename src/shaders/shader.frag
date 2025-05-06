#version 450

// Input color from the vertex shader
layout(location = 0) in vec3 fragColor;

// Output color for the framebuffer attachment
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0); // Use the interpolated color from the vertex shader
}