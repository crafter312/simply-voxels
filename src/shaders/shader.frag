#version 450

// Input from the vertex shader
layout(location = 0) in vec2 fragTexCoord; // Received texture coordinates
layout(location = 1) in vec3 fragNormal;   // Received normal (world space)

// Uniform for the texture sampler
layout(binding = 1) uniform sampler2D texSampler; // New: Texture sampler

// Output color for the framebuffer attachment
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5)); // Example fixed light direction in world space
    vec3 normal = normalize(fragNormal); // Ensure normal is normalized

    // --- Lighting Components ---
    // Ambient light: base light level, affects all surfaces
    vec3 ambientLight = vec3(0.15); // Further lowered ambient for even darker shadows

    // Diffuse light: light that hits the surface and scatters
    float diffuseFactor = max(dot(normal, lightDir), 0.0);
    vec3 diffuseLightColor = vec3(1.0); // Maxed out diffuse for very bright lit areas
    vec3 diffuseContribution = diffuseLightColor * diffuseFactor;
    // --- End Lighting Components ---

    vec4 texColor = texture(texSampler, fragTexCoord);
    // Combine lighting and modulate texture color
    vec3 finalColor = (ambientLight + diffuseContribution) * texColor.rgb;
    outColor = vec4(finalColor, texColor.a);
}