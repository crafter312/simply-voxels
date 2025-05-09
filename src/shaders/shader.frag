#version 450

// Input color from the vertex shader
layout(location = 0) in vec3 fragColor; // Received from vertex shader (can be used for tinting)
layout(location = 1) in vec2 fragTexCoord; // New: Received texture coordinates

// Uniform for the texture sampler
layout(binding = 1) uniform sampler2D texSampler; // New: Texture sampler

// Output color for the framebuffer attachment
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord); // Sample the texture
    // Optionally, you can modulate with the vertex color:
    // outColor = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
}