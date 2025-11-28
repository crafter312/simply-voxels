#ifndef VULKAN_COMMON_HPP
#define VULKAN_COMMON_HPP

// --- Core Vulkan/Windowing Includes ---
// This header should be included by any file that needs Vulkan or GLFW types.

// Tell Vulkan to not define function prototypes, as volk will handle it.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h> // Include the main Vulkan header FIRST to get all type definitions.
#include <volk.h>          // Volk for loading Vulkan functions

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#endif // VULKAN_COMMON_HPP