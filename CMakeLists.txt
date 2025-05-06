cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # Explicitly set policies to 3.10

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

# --- Fetch GLM using FetchContent ---
# Include the module
include(FetchContent)

# Explicitly set where FetchContent stores its stuff (usually defaults to build/_deps)
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)

# Declare the dependency
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1 # Try the latest stable release tag
)

# Explicitly prevent find_package from finding the system GLM
set(glm_DIR "" CACHE INTERNAL "Prevent find_package for system GLM")

# Make it available (downloads/configures if needed)
FetchContent_MakeAvailable(glm)

# Find glslc (SPIR-V compiler) - Keep this as is
find_program(GLSLANG_VALIDATOR_EXECUTABLE glslangValidator HINTS ENV VULKAN_SDK PATHS ENV PATH)
if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator not found. Make sure the Vulkan SDK is installed and in your PATH.")
endif()

# Add executable
add_executable(SimpleVulkanWindow
    src/main.cpp
    src/HelloVulkanApp.cpp
    src/VulkanRenderer.cpp
    src/VulkanSwapChain.cpp
    src/VulkanDevice.cpp
    src/VulkanBufferManager.cpp
    src/VulkanDescriptorSetManager.cpp
    src/VulkanPipelineFactory.cpp) # Add the new pipeline factory source file

# Define shader files
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
set(VERT_SHADER_SRC ${SHADER_DIR}/shader.vert)
set(FRAG_SHADER_SRC ${SHADER_DIR}/shader.frag)
set(VERT_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/vert.spv) # Output to build dir
set(FRAG_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/frag.spv) # Output to build dir

# Ensure output directory exists
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Custom command to compile vertex shader
add_custom_command(
    OUTPUT ${VERT_SHADER_SPV}
    COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${VERT_SHADER_SRC} -o ${VERT_SHADER_SPV}
    DEPENDS ${VERT_SHADER_SRC}
    COMMENT "Compiling vertex shader ${VERT_SHADER_SRC}")

# Custom command to compile fragment shader
add_custom_command(
    OUTPUT ${FRAG_SHADER_SPV}
    COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${FRAG_SHADER_SRC} -o ${FRAG_SHADER_SPV}
    DEPENDS ${FRAG_SHADER_SRC}
    COMMENT "Compiling fragment shader ${FRAG_SHADER_SRC}")

# Custom target to group shader compilation
add_custom_target(Shaders ALL DEPENDS ${VERT_SHADER_SPV} ${FRAG_SHADER_SPV})

# Make the main executable depend on the shaders being compiled
add_dependencies(SimpleVulkanWindow Shaders)

# Link libraries
# Link Vulkan, GLFW, and the fetched GLM target.
# The target name provided by FetchContent for GLM is typically 'glm'.
# Note: GLM is often header-only, but FetchContent sets up an INTERFACE library target.
target_link_libraries(SimpleVulkanWindow PRIVATE Vulkan::Vulkan glfw glm)

message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")