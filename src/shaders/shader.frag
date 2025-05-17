#version 450

// Input from the vertex shader
layout(location = 0) in vec2 fragTexCoord; // Received texture coordinates
layout(location = 1) in vec3 fragNormal;   // Received normal (world space)
layout(location = 2) in vec2 fragAtlasUvOffset; // New input from vertex shader
layout(location = 3) in vec2 fragAtlasUvScale;  // New input from vertex shader

layout(binding = 1) uniform sampler2D texSampler; // Existing texture sampler for the atlas

// Output color for the framebuffer attachment
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5)); // Example fixed light direction in world space
    vec3 normal = normalize(fragNormal); // Ensure normal is normalized

    // Calculate final atlas UVs using the local fragTexCoord and per-vertex atlas parameters
    vec2 final_atlas_uv = fragAtlasUvOffset + fragTexCoord * fragAtlasUvScale;

    // --- Clamping logic ---
    // Define the boundaries of the sub-texture's content area in the atlas UV space
    vec2 min_uv_boundary = fragAtlasUvOffset;
    vec2 max_uv_boundary = fragAtlasUvOffset + fragAtlasUvScale;
    
    // Add a tiny epsilon to max_uv_boundary to ensure robust clamping if scale is extremely small.
    // This helps prevent clamp from collapsing to min_uv_boundary if scale is near zero.
    max_uv_boundary = max(max_uv_boundary, min_uv_boundary + vec2(0.000001f)); 

    final_atlas_uv = clamp(final_atlas_uv, min_uv_boundary, max_uv_boundary);
    // --- End clamping logic ---

    // --- Lighting Components ---
    // Ambient light: base light level, affects all surfaces
    vec3 ambientLight = vec3(0.15); // Further lowered ambient for even darker shadows

    // Diffuse light: light that hits the surface and scatters
    float diffuseFactor = max(dot(normal, lightDir), 0.0);
    vec3 diffuseLightColor = vec3(1.0); // Maxed out diffuse for very bright lit areas
    vec3 diffuseContribution = diffuseLightColor * diffuseFactor;
    // --- End Lighting Components ---

    vec4 texColor = texture(texSampler, final_atlas_uv); // Use the calculated and clamped final_atlas_uv
    // Combine lighting and modulate texture color
    vec3 finalColor = (ambientLight + diffuseContribution) * texColor.rgb;
    outColor = vec4(finalColor, texColor.a);
}