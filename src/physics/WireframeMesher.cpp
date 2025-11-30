 #include "WireframeMesher.hpp"
 #include "VoxelShape.hpp" // Include VoxelShape definition to access BlockAABB
#include <cassert>   // For assert
#include <limits>    // For std::numeric_limits
 
 namespace WireframeMesher {
 
 WireframeMeshData generateVoxelShapeMesh(const Physics::VoxelShape& voxelShape) {
     WireframeMeshData meshData;
 
     if (voxelShape.isEmpty()) {
         return meshData; // Return empty mesh data
     }
 
     for (const auto& aabb : voxelShape.aabbs) {
         // Ensure min/max are correctly ordered (VoxelShape::createCuboidShape does this, but good practice)
         glm::vec3 min = glm::min(aabb.minExtents, aabb.maxExtents);
         glm::vec3 max = glm::max(aabb.minExtents, aabb.maxExtents);
 
         // Define the 8 corners of the AABB
         glm::vec3 c000 = glm::vec3(min.x, min.y, min.z);
         glm::vec3 c100 = glm::vec3(max.x, min.y, min.z);
         glm::vec3 c110 = glm::vec3(max.x, max.y, min.z);
         glm::vec3 c010 = glm::vec3(min.x, max.y, min.z);
         glm::vec3 c001 = glm::vec3(min.x, min.y, max.z);
         glm::vec3 c101 = glm::vec3(max.x, min.y, max.z);
         glm::vec3 c111 = glm::vec3(max.x, max.y, max.z);
         glm::vec3 c011 = glm::vec3(min.x, max.y, max.z);

         // Assert that the number of vertices will not overflow a 32-bit index.
         // This is highly unlikely for a single block's wireframe.
         assert(meshData.vertices.size() <= std::numeric_limits<uint32_t>::max());
 
         // Store the current number of vertices before adding new ones
         uint32_t baseVertexIndex = static_cast<uint32_t>(meshData.vertices.size());
 
         // Add the 8 vertices
         meshData.vertices.push_back({c000, BLACK_COLOR}); // 0
         meshData.vertices.push_back({c100, BLACK_COLOR}); // 1
         meshData.vertices.push_back({c110, BLACK_COLOR}); // 2
         meshData.vertices.push_back({c010, BLACK_COLOR}); // 3
         meshData.vertices.push_back({c001, BLACK_COLOR}); // 4
         meshData.vertices.push_back({c101, BLACK_COLOR}); // 5
         meshData.vertices.push_back({c111, BLACK_COLOR}); // 6
         meshData.vertices.push_back({c011, BLACK_COLOR}); // 7
 
         // Add indices for the 12 edges (Line List topology)
         // Each edge is represented by two indices
 
         // Bottom face (z=min.z)
         meshData.indices.push_back(baseVertexIndex + 0); meshData.indices.push_back(baseVertexIndex + 1); // c000 -> c100
         meshData.indices.push_back(baseVertexIndex + 1); meshData.indices.push_back(baseVertexIndex + 2); // c100 -> c110
         meshData.indices.push_back(baseVertexIndex + 2); meshData.indices.push_back(baseVertexIndex + 3); // c110 -> c010
         meshData.indices.push_back(baseVertexIndex + 3); meshData.indices.push_back(baseVertexIndex + 0); // c010 -> c000
 
         // Top face (z=max.z)
         meshData.indices.push_back(baseVertexIndex + 4); meshData.indices.push_back(baseVertexIndex + 5); // c001 -> c101
         meshData.indices.push_back(baseVertexIndex + 5); meshData.indices.push_back(baseVertexIndex + 6); // c101 -> c111
         meshData.indices.push_back(baseVertexIndex + 6); meshData.indices.push_back(baseVertexIndex + 7); // c111 -> c011
         meshData.indices.push_back(baseVertexIndex + 7); meshData.indices.push_back(baseVertexIndex + 4); // c011 -> c001
 
         // Vertical edges
         meshData.indices.push_back(baseVertexIndex + 0); meshData.indices.push_back(baseVertexIndex + 4); // c000 -> c001
         meshData.indices.push_back(baseVertexIndex + 1); meshData.indices.push_back(baseVertexIndex + 5); // c100 -> c101
         meshData.indices.push_back(baseVertexIndex + 2); meshData.indices.push_back(baseVertexIndex + 6); // c110 -> c111
         meshData.indices.push_back(baseVertexIndex + 3); meshData.indices.push_back(baseVertexIndex + 7); // c010 -> c011
     }
 
     return meshData;
 }
 
 } // namespace WireframeMesher