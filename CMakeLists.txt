cmake_minimum_required(VERSION 3.10)

# Project name and language
project(SimpleVulkanWindow LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan package
# Ensure Vulkan SDK is installed and VULKAN_SDK environment variable is set
find_package(Vulkan REQUIRED)

# Find GLFW package (make sure GLFW is installed or provide it)
find_package(glfw3 REQUIRED)

# Add executable
add_executable(SimpleVulkanWindow
    src/main.cpp
    src/HelloVulkanApp.cpp
    src/VulkanRenderer.cpp) # Add the new renderer source file

# Link libraries
target_link_libraries(SimpleVulkanWindow PRIVATE Vulkan::Vulkan glfw)

message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")