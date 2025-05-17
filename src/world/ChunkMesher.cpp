#include "ChunkMesher.hpp"
#include "World.hpp" // For World class definition (to query neighbors)
#include "../resource/ResourceManager.hpp" // For ResourceManager definition
#include "../block/Blocks.hpp" // For Blocks::AIR_ID
#include "../block/BlockRegistry.hpp" // Include for BlockRegistry type

#include <array>
#include <iostream> // For debugging

namespace ChunkMesher {

/**
 * @brief Adds the vertices and indices of a given block model to the chunk's mesh data.
 * 
 * The model's vertices are offset by the block's local position within the chunk.
 * Vertex normals and texture coordinates are taken directly from the loaded model.
 * 
 * @param chunkMeshData The mesh data for the entire chunk, to which this block's model will be added.
 * @param blockModelData The pre-loaded model data (vertices, indices, normals, UVs) for the block type.
 * @param blockLocalOffset The integer local coordinates (e.g., 0-15) of this block within the chunk.
 */
void addBlockModelToMeshData(ModelData& chunkMeshData,
                             const ModelData& blockModelData,
                             const glm::vec3& blockLocalOffset) { // Removed atlasInfo parameter
    if (blockModelData.vertices.empty()) { // No need to check indices if no vertices
        // std::cout << "Debug: Block model has no vertices. Skipping." << std::endl;
        return; 
    }
    if (blockModelData.indices.empty()) {
        // std::cout << "Debug: Block model has no indices. Skipping." << std::endl;
        return;
    }

    uint32_t baseVertexIndex = static_cast<uint32_t>(chunkMeshData.vertices.size());

    for (size_t i = 0; i < blockModelData.vertices.size(); ++i) {
        const auto& modelVertex = blockModelData.vertices[i];
        // const auto& modelAtlasInfo = blockModelData.atlasTextureInfos[i]; // This line is no longer needed as ModelData doesn't have this vector

        Vertex chunkVertex;
        // Offset the model's vertex positions by the block's local position in the chunk
        chunkVertex.pos = modelVertex.pos + blockLocalOffset;
        chunkVertex.normal = modelVertex.normal;     // Use normals from the model
        // Store the original local [0,1] texture coordinates.
        // The transformation using atlas offset/scale will happen in the shader via per-vertex AtlasTextureInfo.
        chunkVertex.texCoord = modelVertex.texCoord;
        // The atlasUvOffset and atlasUvScale are now part of modelVertex itself,
        // so they are implicitly copied when modelVertex is used to construct/assign to chunkVertex,
        // or explicitly copied if needed (as shown below, which is good for clarity).
        chunkVertex.atlasUvOffset = modelVertex.atlasUvOffset;
        chunkVertex.atlasUvScale = modelVertex.atlasUvScale;
        chunkMeshData.vertices.push_back(chunkVertex);
    }

    for (uint32_t modelIndex : blockModelData.indices) {
        chunkMeshData.indices.push_back(baseVertexIndex + modelIndex);
    }
}

// Helper struct to cache data for neighbor chunks
struct CachedNeighborData {
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> blockDataSnapshot = nullptr;
    bool isAllAir = true; // Default to true, meaning if not found or not generated, effectively air for culling
    bool isGeneratedAndExists = false; // True if the chunk exists and has finished generation
};
ModelData generateMesh(
    glm::ivec3 chunkCoord, // Use passed chunkCoord
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> currentChunkBlockDataSnapshot, // Renamed for clarity
    bool isCurrentChunkAllAir,    // Renamed for clarity
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry) { // Added blockRegistry
    ModelData meshData;

    if (isCurrentChunkAllAir || !currentChunkBlockDataSnapshot) {
        return meshData; // Empty mesh for all-air or no data chunks
    }

    // Calculate chunk's world origin using its coordinates
    glm::ivec3 chunkWorldOrigin = glm::ivec3(
        chunkCoord.x * CHUNK_WIDTH,
        chunkCoord.y * CHUNK_HEIGHT,
        chunkCoord.z * CHUNK_DEPTH
    );

    // 1. Cache data for 6 adjacent neighbor chunks
    std::array<CachedNeighborData, 6> neighborCache;
    for (int i = 0; i < 6; ++i) { // NEIGHBOR_OFFSETS has 6 elements
        glm::ivec3 neighborChunkGlobalCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
        const Chunk* neighborChunkPtr = world.getChunk(neighborChunkGlobalCoord);

        if (neighborChunkPtr && neighborChunkPtr->isGenerated()) {
            neighborCache[i].isGeneratedAndExists = true;
            neighborCache[i].isAllAir = neighborChunkPtr->isAllAir(); // Store this first
            if (!neighborCache[i].isAllAir) { // Only get snapshot if not all air
                 neighborCache[i].blockDataSnapshot = neighborChunkPtr->getBlockDataSnapshot();
                 // If snapshot is null even if not allAir, it means it became allAir very recently
                 // or there's an issue. isAllAir flag should be primary.
                 if (!neighborCache[i].blockDataSnapshot) neighborCache[i].isAllAir = true;
            }
        } else {
            neighborCache[i].isGeneratedAndExists = false;
            // Defaults (isAllAir = true, blockDataSnapshot = nullptr) are fine.
        }
    }

    // 2. Iterate through blocks in the current chunk using nested loops
    for (int z = 0; z < CHUNK_DEPTH; ++z) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int x = 0; x < CHUNK_WIDTH; ++x) {
                size_t blockIndex = Chunk::localToIndex(x, y, z);
                uint16_t blockID = (*currentChunkBlockDataSnapshot)[blockIndex];

                if (blockID == Blocks::AIR_ID) {
                    continue;
                }

                glm::ivec3 currentBlockLocalPos_ivec(x, y, z);
                
                bool cellFaceIsActuallyExposed[6] = {false}; // True if this cell face is open to air/non-occluding neighbor
                bool anyCellFaceIsActuallyExposed = false;

                // 3. Determine exposure status for all 6 conceptual cell faces
                for (int faceIndex = 0; faceIndex < 6; ++faceIndex) { // Iterate 6 faces
                    glm::ivec3 neighborOffset = NEIGHBOR_OFFSETS[faceIndex];
                    glm::ivec3 neighborLocalPosRelativeToCurrentChunk = currentBlockLocalPos_ivec + neighborOffset;
                    uint16_t neighborBlockID_val;
                    bool neighborOccludesThisFace = true; // Assume occluded by default

                    // Check if neighbor is within the current chunk
                    if (neighborLocalPosRelativeToCurrentChunk.x >= 0 && neighborLocalPosRelativeToCurrentChunk.x < CHUNK_WIDTH &&
                        neighborLocalPosRelativeToCurrentChunk.y >= 0 && neighborLocalPosRelativeToCurrentChunk.y < CHUNK_HEIGHT &&
                        neighborLocalPosRelativeToCurrentChunk.z >= 0 && neighborLocalPosRelativeToCurrentChunk.z < CHUNK_DEPTH) {
                        
                        size_t neighborIdxInSnapshot = Chunk::localToIndex(
                            neighborLocalPosRelativeToCurrentChunk.x,
                            neighborLocalPosRelativeToCurrentChunk.y,
                            neighborLocalPosRelativeToCurrentChunk.z
                        );
                        neighborBlockID_val = (*currentChunkBlockDataSnapshot)[neighborIdxInSnapshot];
                        
                        if (neighborBlockID_val == Blocks::AIR_ID) {
                            neighborOccludesThisFace = false;
                        } else {
                            // Check if the solid neighbor has a full face pointing back at us
                            neighborOccludesThisFace = blockRegistry.isBlockFaceFull(neighborBlockID_val, Block::getOppositeFace(static_cast<FaceDirection>(faceIndex)));
                        }
                    } else {
                        // Neighbor is in an adjacent chunk, use the cache
                        const CachedNeighborData& cachedNeighbor = neighborCache[faceIndex];

                        if (!cachedNeighbor.isGeneratedAndExists) {
                            neighborOccludesThisFace = true; // Treat as occluded if neighbor chunk not ready/non-existent
                        } else {
                            if (cachedNeighbor.isAllAir) {
                                neighborBlockID_val = Blocks::AIR_ID;
                                neighborOccludesThisFace = false;
                            } else {
                                // Neighbor chunk exists, is generated, and not all air. Get its block ID.
                                // This part assumes blockDataSnapshot is valid if not allAir.
                                // Calculate local coordinates within the *neighbor* chunk
                                // (coord + CHUNK_DIM) % CHUNK_DIM handles negative results from subtraction correctly for modulo.
                                glm::ivec3 localPosInNeighborChunk(
                                    (currentBlockLocalPos_ivec.x + neighborOffset.x + CHUNK_WIDTH) % CHUNK_WIDTH,
                                    (currentBlockLocalPos_ivec.y + neighborOffset.y + CHUNK_HEIGHT) % CHUNK_HEIGHT,
                                    (currentBlockLocalPos_ivec.z + neighborOffset.z + CHUNK_DEPTH) % CHUNK_DEPTH
                                );
                                
                                if (cachedNeighbor.blockDataSnapshot) { // Check if snapshot was actually retrieved
                                    neighborBlockID_val = (*cachedNeighbor.blockDataSnapshot)[Chunk::localToIndex(
                                        localPosInNeighborChunk.x,
                                        localPosInNeighborChunk.y,
                                        localPosInNeighborChunk.z
                                    )];
                                    if (neighborBlockID_val == Blocks::AIR_ID) { // Should be rare if not isAllAir
                                        neighborOccludesThisFace = false;
                                    } else {
                                        neighborOccludesThisFace = blockRegistry.isBlockFaceFull(neighborBlockID_val, Block::getOppositeFace(static_cast<FaceDirection>(faceIndex)));
                                    }
                                } else {
                                    // Should be caught by isAllAir, but as a fallback:
                                    neighborBlockID_val = Blocks::AIR_ID;
                                    neighborOccludesThisFace = false; // Effectively air
                                }
                            }
                        }
                    }

                    if (!neighborOccludesThisFace) {
                        cellFaceIsActuallyExposed[faceIndex] = true;
                        anyCellFaceIsActuallyExposed = true;
                        // DO NOT break; we need to check all 6 faces.
                    }
                } // End face check loop

                if (anyCellFaceIsActuallyExposed) {
                    // Get the SeparableModelData
                    std::shared_ptr<const SeparableModelData> separableModelDataPtr = resourceManager.getModelForBlockType(blockID);
                    // AtlasTextureInfo atlasInfo = resourceManager.getBlockAtlasInfo(blockID); // No longer needed here
                    const Block* blockDef = blockRegistry.getBlockDefinition(blockID); // Get block definition

                    if (separableModelDataPtr && blockDef) { // Check if the model data and block def were successfully loaded
                        // Convert ivec3 to vec3 for addBlockModelToMeshData
                        glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

                        // 1. Add specific "full" canonical faces if they are defined for the block AND the cell face is exposed
                        for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                            if (cellFaceIsActuallyExposed[faceIdx]) { // If this specific cell face is open
                                FaceDirection currentBlockDir = static_cast<FaceDirection>(faceIdx);
                                // And if the current block *has* a defined canonical face for this direction
                                if (blockDef->hasFullOccludingFace(currentBlockDir)) { 
                                    const ModelData& faceGeom = separableModelDataPtr->canonicalFaces[faceIdx];
                                    // The faceGeom.atlasTextureInfos should be populated by ResourceManager
                                    addBlockModelToMeshData(meshData, faceGeom, currentBlockLocalPos_vec3);
                                }
                            }
                        }

                        // 2. Add the "remaining" geometry (non-canonical parts, or whole model if no full faces)
                        //    This part is added if *any* cell face was exposed.
                        // The remainingGeometry.atlasTextureInfos should be populated by ResourceManager
                        addBlockModelToMeshData(meshData, separableModelDataPtr->remainingGeometry, currentBlockLocalPos_vec3);
                    }
                }
            }
        } // End y loop
    } // End z loop
    return meshData;
}

} // namespace ChunkMesher