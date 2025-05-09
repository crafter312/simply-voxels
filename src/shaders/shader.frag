#version 450

// Input from the vertex shader
layout(location = 0) in vec2 fragTexCoord; // Received texture coordinates (was location 1)

// Uniform for the texture sampler
layout(binding = 1) uniform sampler2D texSampler; // New: Texture sampler

// Output color for the framebuffer attachment
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord); // Sample the texture
    // If you ever wanted to tint, you'd need a uniform or another way to pass color data.
}