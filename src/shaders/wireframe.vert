 #version 450
 
 layout(location = 0) in vec3 inPosition;
 layout(location = 1) in vec3 inColor;
 
 layout(location = 0) out vec3 fragColor;
 
 layout(set = 0, binding = 0) uniform UniformBufferObject {
     mat4 view;
     mat4 proj;
 } ubo;
 
 layout(push_constant) uniform PushConstants {
     mat4 model;
 } pc;
 
 void main() {
     gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
     fragColor = inColor;
     // Optional: Add a tiny offset to prevent z-fighting if needed,
     // though depthWrite=false and depthCompare=LESS_OR_EQUAL should handle most cases.
     // gl_Position.z -= gl_Position.w * 0.0001;
 }