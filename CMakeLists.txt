cmake_minimum_required(VERSION 3.11 FATAL_ERROR) # FetchContent requires at least 3.11

# Project name and language
project(SimpleVulkanWindow LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan package
# Ensure Vulkan SDK is installed and VULKAN_SDK environment variable is set
find_package(Vulkan REQUIRED)

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

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4 # Using latest release tag for GLFW
)

# Explicitly prevent find_package from finding the system GLM
set(glm_DIR "" CACHE INTERNAL "Prevent find_package for system GLM")

# Make it available (downloads/configures if needed)
FetchContent_MakeAvailable(glm)

# GLFW options (to build only the library, not examples/tests/docs)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation" FORCE)
FetchContent_MakeAvailable(glfw)

# LZ4 options (to build only the static library, disable CLI and tests)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build LZ4 as a shared library" FORCE)
set(LZ4_BUILD_CLI OFF CACHE BOOL "Build LZ4 command line interface" FORCE)
set(LZ4_SKIP_TESTS ON CACHE BOOL "Skip LZ4 tests" FORCE)

# Populate LZ4 directly, providing all details.
# This avoids the deprecation warning associated with calling FetchContent_Populate
# on a previously declared FetchContent_Declare name.
FetchContent_Populate(
  lz4  # This name is used to derive lz4_SOURCE_DIR and lz4_BINARY_DIR
  GIT_REPOSITORY https://github.com/lz4/lz4.git
  GIT_TAG        v1.9.4
  GIT_SHALLOW    FALSE
  GIT_PROGRESS   TRUE
)

set(LZ4_CMAKE_SUBDIR "build/cmake") # LZ4's CMakeLists.txt is in a subdirectory

cmake_policy(PUSH) # Save current policy settings
cmake_policy(VERSION ${CMAKE_VERSION}) # Set policy version to current CMake version
add_subdirectory(${lz4_SOURCE_DIR}/${LZ4_CMAKE_SUBDIR} ${lz4_BINARY_DIR}/lz4-build) # Explicitly add lz4 as a subproject, adjust binary dir
cmake_policy(POP) # Restore previous policy settings

# --- Manage stb_image.h ---
# Define where stb_image.h will be stored locally
set(STB_IMAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
set(STB_IMAGE_HEADER ${STB_IMAGE_DIR}/stb_image.h)

# Ensure the local directory exists
file(MAKE_DIRECTORY ${STB_IMAGE_DIR})

# Download stb_image.h if it doesn't exist
if(NOT EXISTS ${STB_IMAGE_HEADER})
    message(STATUS "Downloading stb_image.h")
    file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
         ${STB_IMAGE_HEADER}
         SHOW_PROGRESS
         STATUS stb_download_status)
    list(GET stb_download_status 0 stb_error_code)
    if(NOT stb_error_code EQUAL 0)
        message(FATAL_ERROR "Failed to download stb_image.h. Error code: ${stb_error_code}")
    endif()
endif()

# Create an interface library for stb_image.h
# This makes it easy to link against and provides the necessary include directory.
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${STB_IMAGE_DIR})

# --- Manage tinygltf.h ---
# Define where tinygltf.h will be stored locally
set(TINYGLTF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf)
set(TINYGLTF_HEADER ${TINYGLTF_DIR}/tinygltf.h)
set(NLOHMANN_JSON_HEADER ${TINYGLTF_DIR}/json.hpp) # nlohmann/json header will go here

# Ensure the local directory exists
file(MAKE_DIRECTORY ${TINYGLTF_DIR})

# Download tinygltf.h if it doesn't exist
if(NOT EXISTS ${TINYGLTF_HEADER})
    message(STATUS "Downloading tinygltf.h")
    file(DOWNLOAD https://raw.githubusercontent.com/syoyo/tinygltf/release/tiny_gltf.h # Use the header from the release branch
         ${TINYGLTF_HEADER}
         SHOW_PROGRESS
         STATUS tinygltf_download_status)
    list(GET tinygltf_download_status 0 tinygltf_error_code)
    if(NOT tinygltf_error_code EQUAL 0)
        message(FATAL_ERROR "Failed to download tinygltf.h. Error code: ${tinygltf_error_code}")
    endif()
endif()

# Download nlohmann/json.hpp into the same directory as tiny_gltf.h
# This allows tiny_gltf.h to find it with #include "json.hpp"
if(NOT EXISTS ${NLOHMANN_JSON_HEADER})
    message(STATUS "Downloading nlohmann/json.hpp to ${NLOHMANN_JSON_HEADER}")
    file(DOWNLOAD https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp # Use the same version tag
         ${NLOHMANN_JSON_HEADER}
         SHOW_PROGRESS
         STATUS nlohmann_json_download_status)
    list(GET nlohmann_json_download_status 0 nlohmann_json_error_code)
    if(NOT nlohmann_json_error_code EQUAL 0)
        message(FATAL_ERROR "Failed to download nlohmann/json.hpp. Error code: ${nlohmann_json_error_code}")
    endif()
endif()

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${TINYGLTF_DIR})
# Find glslc (SPIR-V compiler) - Keep this as is
find_program(GLSLANG_VALIDATOR_EXECUTABLE glslangValidator HINTS ENV VULKAN_SDK PATHS ENV PATH)
if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator not found. Make sure the Vulkan SDK is installed and in your PATH.")
endif()

# Add executable
add_executable(SimpleVulkanWindow
    src/main.cpp
    src/HelloVulkanApp.cpp
    src/VulkanDebug.cpp                  # Add the new VulkanDebug source file
    src/render/VulkanRenderer.cpp
    src/render/VulkanSwapChain.cpp
    src/render/VulkanDevice.cpp
    src/render/VulkanBufferManager.cpp
    src/render/VulkanDescriptorSetManager.cpp
    src/render/VulkanPipelineFactory.cpp
    src/resource/VulkanTextureLoader.cpp # Add the new texture loader
    src/resource/ModelLoader.cpp         # Add the new model loader
    src/resource/ResourceManager.cpp     # Add ResourceManager
    src/InputManager.cpp 
    src/Player.cpp                       # Add the new Player source file
    src/Camera.cpp                       # Add the new Camera source file
    src/block/Block.cpp                        # Source file for the Block class
    src/block/Blocks.cpp                       # Source file for the Blocks namespace (e.g., registration)
    src/block/BlockRegistry.cpp                # Add BlockRegistry class
    src/world/World.cpp                  # Add World class
    src/world/Chunk.cpp                  # Add Chunk class
    src/world/ChunkMesher.cpp            # Add ChunkMesher namespace/functions
    src/resource/RegionManager.cpp          # Add RegionManager class
    src/util/Raycaster.cpp               # Add the new Raycaster source file
    src/physics/VoxelShape.cpp           # Add the new VoxelShape source file
)

# Add compile definitions for tinygltf configuration to the target
# TINYGLTF_IMPLEMENTATION must remain defined in only one .cpp file (ModelLoader.cpp)
target_compile_definitions(SimpleVulkanWindow PRIVATE
    TINYGLTF_NO_STB_IMAGE
    TINYGLTF_NO_STB_IMAGE_WRITE
    TINYGLTF_NO_EXTERNAL_IMAGE)

# Define shader files
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
set(VERT_SHADER_SRC ${SHADER_DIR}/shader.vert)
set(FRAG_SHADER_SRC ${SHADER_DIR}/shader.frag)
set(WIREFRAME_VERT_SHADER_SRC ${SHADER_DIR}/wireframe.vert) # New wireframe vertex shader
set(WIREFRAME_FRAG_SHADER_SRC ${SHADER_DIR}/wireframe.frag) # New wireframe fragment shader
set(VERT_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/vert.spv) # Output to build dir
set(FRAG_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/frag.spv) # Output to build dir
set(WIREFRAME_VERT_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/wireframe_vert.spv) # Output to build dir
set(WIREFRAME_FRAG_SHADER_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/wireframe_frag.spv) # Output to build dir

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

# Custom command to compile wireframe vertex shader
add_custom_command(
    OUTPUT ${WIREFRAME_VERT_SHADER_SPV}
    COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${WIREFRAME_VERT_SHADER_SRC} -o ${WIREFRAME_VERT_SHADER_SPV}
    DEPENDS ${WIREFRAME_VERT_SHADER_SRC}
    COMMENT "Compiling wireframe vertex shader ${WIREFRAME_VERT_SHADER_SRC}")

# Custom command to compile wireframe fragment shader
add_custom_command(
    OUTPUT ${WIREFRAME_FRAG_SHADER_SPV}
    COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${WIREFRAME_FRAG_SHADER_SRC} -o ${WIREFRAME_FRAG_SHADER_SPV}
    DEPENDS ${WIREFRAME_FRAG_SHADER_SRC}
    COMMENT "Compiling wireframe fragment shader ${WIREFRAME_FRAG_SHADER_SRC}")

# Custom target to group shader compilation
add_custom_target(Shaders ALL DEPENDS ${VERT_SHADER_SPV} ${FRAG_SHADER_SPV} ${WIREFRAME_VERT_SHADER_SPV} ${WIREFRAME_FRAG_SHADER_SPV})

# Link libraries
# Link Vulkan, GLFW, and the fetched GLM target.
# The target name provided by FetchContent for GLM is typically 'glm'.
# Note: GLM, stb_image, and tinygltf are header-only (or nearly so), but we set up INTERFACE library targets for include management. LZ4 is found via pkg-config.
target_link_libraries(SimpleVulkanWindow PRIVATE Vulkan::Vulkan glfw glm stb_image tinygltf lz4_static)

message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")