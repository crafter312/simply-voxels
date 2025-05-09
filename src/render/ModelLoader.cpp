#include "ModelLoader.hpp"

#define TINYGLTF_IMPLEMENTATION
// The other TINYGLTF_NO_ defines are now handled by CMake target_compile_definitions
#include "tinygltf.h" // Should be in your include path

#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::make_vec3 and glm::make_vec2

namespace ModelLoader {

bool loadGltfModel(const std::string& filepath, ModelData& outModelData) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool res = loader.LoadBinaryFromFile(&model, &err, &warn, filepath);

    if (!warn.empty()) {
        std::cout << "TinyGLTF Warning: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "TinyGLTF Error: " << err << std::endl;
        return false;
    }

    if (!res) {
        std::cerr << "Failed to load GLTF model: " << filepath << std::endl;
        return false;
    }

    outModelData.vertices.clear();
    outModelData.indices.clear();

    // For simplicity, we'll load all primitives from the first mesh.
    // A more robust loader would handle multiple meshes, transformations, etc.
    if (model.meshes.empty()) {
        std::cerr << "No meshes found in GLTF model: " << filepath << std::endl;
        return false;
    }

    const tinygltf::Mesh& mesh = model.meshes[0]; // Using the first mesh

    for (const auto& primitive : mesh.primitives) {
        uint32_t firstIndex = static_cast<uint32_t>(outModelData.vertices.size());

        // Vertices
        const float* positionBuffer = nullptr;
        const float* texCoordBuffer = nullptr;
        size_t vertexCount = 0;

        if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
            const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            positionBuffer = reinterpret_cast<const float*>(&(model.buffers[bufferView.buffer].data[accessor.byteOffset + bufferView.byteOffset]));
            vertexCount = accessor.count;
        } else {
            std::cerr << "Primitive has no POSITION attribute." << std::endl;
            continue; // Skip this primitive
        }

        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
            const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            texCoordBuffer = reinterpret_cast<const float*>(&(model.buffers[bufferView.buffer].data[accessor.byteOffset + bufferView.byteOffset]));
        } else {
            std::cerr << "Primitive has no TEXCOORD_0 attribute. Using (0,0)." << std::endl;
            // texCoordBuffer will remain nullptr, handled below
        }

        for (size_t v = 0; v < vertexCount; ++v) {
            Vertex vert{};
            vert.pos = glm::make_vec3(&positionBuffer[v * 3]);
            if (texCoordBuffer) {
                vert.texCoord = glm::make_vec2(&texCoordBuffer[v * 2]);
            } else {
                vert.texCoord = glm::vec2(0.0f, 0.0f); // Default tex coords
            }
            outModelData.vertices.push_back(vert);
        }

        // Indices
        if (primitive.indices >= 0) {
            const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
            const void* dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

            switch (accessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                    const uint8_t* buf = static_cast<const uint8_t*>(dataPtr);
                    for (size_t index = 0; index < accessor.count; ++index) {
                        outModelData.indices.push_back(firstIndex + buf[index]);
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                    const uint16_t* buf = static_cast<const uint16_t*>(dataPtr);
                    for (size_t index = 0; index < accessor.count; ++index) {
                        outModelData.indices.push_back(firstIndex + buf[index]);
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                    const uint32_t* buf = static_cast<const uint32_t*>(dataPtr);
                    for (size_t index = 0; index < accessor.count; ++index) {
                        outModelData.indices.push_back(firstIndex + buf[index]);
                    }
                    break;
                }
                default:
                    std::cerr << "Index accessor component type " << accessor.componentType << " not supported." << std::endl;
                    return false;
            }
        } else {
             // Non-indexed geometry; generate trivial indices
            for(size_t i = 0; i < vertexCount; ++i) {
                outModelData.indices.push_back(firstIndex + i);
            }
        }
    }

    if (outModelData.vertices.empty()) {
        std::cerr << "No vertices loaded from model: " << filepath << std::endl;
        return false;
    }

    std::cout << "Loaded model: " << filepath << " ("
              << outModelData.vertices.size() << " vertices, "
              << outModelData.indices.size() << " indices)" << std::endl;

    return true;
}

} // namespace ModelLoader