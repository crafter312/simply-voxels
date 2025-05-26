#ifndef CHUNK_MESHER_HPP
#define CHUNK_MESHER_HPP

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <array>       // For std::array
#include <memory>      // For std::unique_ptr (used by included Chunk.hpp)
#include <optional>    // For std::optional

// Forward declarations to avoid circular dependencies
// Chunk.hpp is now included directly for CHUNK_VOLUME
#include "Chunk.hpp" // Include Chunk.hpp for CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH, CHUNK_VOLUME
#include "../block/Block.hpp" // For FaceDirection
#include "../resource/ModelLoader.hpp" 

class World;
class ResourceManager; // Assuming ResourceManager provides block properties and texture atlas info
class BlockRegistry;   // Forward declare BlockRegistry
// ModelData is now included via ModelLoader.hpp

namespace ChunkMesher {

// Helper to define offsets to neighbor chunks
const std::array<glm::ivec3, 6> NEIGHBOR_OFFSETS = {{
    {1, 0, 0}, {-1, 0, 0},  // Right, Left
    {0, 1, 0}, {0, -1, 0},  // Up, Down
    {0, 0, 1}, {0, 0, -1}   // Front, Back
}};

// Helper struct to cache data for neighbor chunks
struct CachedNeighborData {
    // Stores the 2D plane of block IDs from the neighbor chunk
    // that is adjacent to the current chunk being meshed.
    // Size is CHUNK_SIDE_LENGTH * CHUNK_SIDE_LENGTH (e.g., 16*16 = 256 for one face).
    std::optional<std::array<uint16_t, CHUNK_SIDE_LENGTH * CHUNK_SIDE_LENGTH>> boundaryPlaneData;
    bool isAllAir = true; // Default to true, meaning if not found or not generated, effectively air for culling
    bool isGeneratedAndExists = false; // True if the chunk exists and has finished generation
};

// Internal helper structs and tables for boundary plane caching logic
namespace Detail { // Using a detail namespace to encapsulate these implementation details

struct PlaneCopyConfig {
    int src_fixed_dim_axis;   // 0 for X, 1 for Y, 2 for Z (axis in neighbor_full_block_data_ptr that is constant for the plane)
    int src_fixed_dim_value;  // Value of that constant axis (0 or CHUNK_SIDE_LENGTH - 1)
    int plane_iter_axis1;     // Axis for outer loop iterator (e.g., Y if src_fixed_dim_axis is X)
    int plane_iter_axis2;     // Axis for inner loop iterator (e.g., Z if src_fixed_dim_axis is X)
};

// Indexed by FaceDirection (direction *to* the neighbor chunk)
// This table defines how to extract a plane from the *neighbor* chunk's 3D data.
static const std::array<PlaneCopyConfig, 6> PLANE_COPY_CONFIGS = {{
    // Facing POS_X (neighbor is at +X of current): Copy neighbor's local X=0 plane (a YZ plane). iter1=Y, iter2=Z
    {/*src_fixed_dim_axis=*/0, /*src_fixed_dim_value=*/0,                           /*plane_iter_axis1=*/1, /*plane_iter_axis2=*/2},
    // Facing NEG_X (neighbor is at -X of current): Copy neighbor's local X=CHUNK_SIDE_LENGTH-1 plane (a YZ plane). iter1=Y, iter2=Z
    {/*src_fixed_dim_axis=*/0, /*src_fixed_dim_value=*/CHUNK_SIDE_LENGTH - 1,       /*plane_iter_axis1=*/1, /*plane_iter_axis2=*/2},
    // Facing POS_Y (neighbor is at +Y of current): Copy neighbor's local Y=0 plane (an XZ plane). iter1=X, iter2=Z
    {/*src_fixed_dim_axis=*/1, /*src_fixed_dim_value=*/0,                           /*plane_iter_axis1=*/0, /*plane_iter_axis2=*/2},
    // Facing NEG_Y (neighbor is at -Y of current): Copy neighbor's local Y=CHUNK_SIDE_LENGTH-1 plane (an XZ plane). iter1=X, iter2=Z
    {/*src_fixed_dim_axis=*/1, /*src_fixed_dim_value=*/CHUNK_SIDE_LENGTH - 1,       /*plane_iter_axis1=*/0, /*plane_iter_axis2=*/2},
    // Facing POS_Z (neighbor is at +Z of current): Copy neighbor's local Z=0 plane (an XY plane). iter1=X, iter2=Y
    {/*src_fixed_dim_axis=*/2, /*src_fixed_dim_value=*/0,                           /*plane_iter_axis1=*/0, /*plane_iter_axis2=*/1},
    // Facing NEG_Z (neighbor is at -Z of current): Copy neighbor's local Z=CHUNK_SIDE_LENGTH-1 plane (an XY plane). iter1=X, iter2=Y
    {/*src_fixed_dim_axis=*/2, /*src_fixed_dim_value=*/CHUNK_SIDE_LENGTH - 1,       /*plane_iter_axis1=*/0, /*plane_iter_axis2=*/1}
}};

struct PlaneAccessConfig {
    int current_block_u_coord_component; // Component of currentBlockLocalPos_ivec for plane's u (0 for .x, 1 for .y, 2 for .z)
    int current_block_v_coord_component; // Component of currentBlockLocalPos_ivec for plane's v
};

// Indexed by FaceDirection (direction *of the face* of the current block being checked)
// This table defines how to index into the cached 2D plane using the current block's local coords.
static const std::array<PlaneAccessConfig, 6> PLANE_ACCESS_CONFIGS = {{
    {1, 2}, {1, 2}, // POS_X, NEG_X (YZ plane: u=y, v=z)
    {0, 2}, {0, 2}, // POS_Y, NEG_Y (XZ plane: u=x, v=z)
    {0, 1}, {0, 1}  // POS_Z, NEG_Z (XY plane: u=x, v=y)
}};
} // namespace Detail

/**
 * @brief Generates renderable mesh data for a given chunk.
 *
 * This function iterates through the blocks in the chunk, performs face culling
 * against neighboring blocks (both within the chunk and in adjacent chunks via the World object),
 * and adds the model data for exposed blocks.
 * 
 * @param chunkCoord The coordinates of the chunk being meshed.
 * @param currentChunk The actual Chunk object to be meshed.
 * @param world The World object, used to query blocks in neighboring chunks for face culling.
 * @param resourceManager The ResourceManager, used to get block properties and texture information.
 * @param blockRegistry The BlockRegistry, used to query block face properties.
 * @return MeshData A struct containing the generated vertices and indices.
 * @return ModelData A struct containing the generated vertices, indices, and per-vertex atlas info.
 */
ModelData generateMesh(
    glm::ivec3 chunkCoord, // Still useful for neighbor lookups and context
    const Chunk& currentChunk,
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry); // Added blockRegistry

// Function to add an entire block's model to the chunk's mesh data
void addBlockModelToMeshData(ModelData& chunkMeshData,
                            const ModelData& blockModelData, // ModelData struct should be defined (e.g. via ModelLoader.hpp)
                            const glm::vec3& blockLocalOffset); // AtlasInfo is now part of blockModelData

} // namespace ChunkMesher

#endif // CHUNK_MESHER_HPP