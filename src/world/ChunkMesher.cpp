#include "ChunkMesher.hpp"
#include "World.hpp" // For World class definition (to query neighbors)
#include "../resource/ResourceManager.hpp" // For ResourceManager definition
#include "../block/Blocks.hpp" // For Blocks::AIR_ID
#include "../block/BlockRegistry.hpp" // Include for BlockRegistry type

#include <array>
#include <optional>  // For std::optional

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

ModelData generateMesh(
    glm::ivec3 chunkCoord, // Chunk coordinates of currentChunk
    const Chunk& currentChunk,
    const World& world,
    const ResourceManager& resourceManager,
    const BlockRegistry& blockRegistry) { // Added blockRegistry
    ModelData meshData;

    // Reserve memory to reduce reallocations. These are heuristics and can be tuned.
    // CHUNK_VOLUME is CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH (e.g., 16*16*16 = 4096).
    //
    // Estimate based on a fraction of blocks being solid and exposing a few faces:
    //   Vertices reserve: CHUNK_VOLUME * 4 (e.g., 4096 * 4 = 16384)
    //   Indices reserve:  CHUNK_VOLUME * 6 (e.g., 4096 * 6 = 24576)
    //
    // These values are chosen as a starting point. Profiling actual mesh sizes for
    // typical and dense chunks in your world can help refine these estimates.
    meshData.vertices.reserve(static_cast<size_t>(CHUNK_VOLUME * 4));
    meshData.indices.reserve(static_cast<size_t>(CHUNK_VOLUME * 6));

    if (currentChunk.isAllAir()) {
        return meshData; // Empty mesh for all-air or no data chunks
    }

    // 1. Cache data for 6 adjacent neighbor chunks
    std::array<CachedNeighborData, 6> neighborCache;
    for (int i = 0; i < 6; ++i) { // NEIGHBOR_OFFSETS has 6 elements
        glm::ivec3 neighborChunkGlobalCoord = currentChunk.getChunkCoord() + NEIGHBOR_OFFSETS[i];
        std::shared_ptr<const Chunk> neighborChunk_sptr = world.getChunk(neighborChunkGlobalCoord); // Changed to shared_ptr

        if (neighborChunk_sptr && neighborChunk_sptr->isGenerated()) {
            neighborCache[i].isGeneratedAndExists = true;
            neighborCache[i].isAllAir = neighborChunk_sptr->isAllAir(); // Store this first
            if (!neighborCache[i].isAllAir) { // Only get snapshot if not all air
                neighborChunk_sptr->performLockedRead( // Use shared_ptr here
                    [&, i](const uint16_t* neighbor_full_block_data_ptr, size_t capacity) {
                        if (!neighbor_full_block_data_ptr || capacity != CHUNK_VOLUME) {
                            // If data is null or capacity mismatch, treat as all air for safety
                            neighborCache[i].isAllAir = true;
                            neighborCache[i].boundaryPlaneData = std::nullopt;
                            return;
                        }

                        neighborCache[i].boundaryPlaneData.emplace(); // Create the plane array
                        auto& plane_data_ref = *neighborCache[i].boundaryPlaneData;
                        
                        // Determine which plane to copy based on neighbor index 'i'
                        // FaceDirection enum values match NEIGHBOR_OFFSETS indices
                        FaceDirection dir_to_neighbor = static_cast<FaceDirection>(i); // This 'i' is the index for NEIGHBOR_OFFSETS
                        const auto& copy_config = Detail::PLANE_COPY_CONFIGS[static_cast<size_t>(dir_to_neighbor)];

                        glm::ivec3 src_coord_3d; // Reusable vector for source coordinates in neighbor
                        src_coord_3d[copy_config.src_fixed_dim_axis] = copy_config.src_fixed_dim_value;

                        for (int iter1_val = 0; iter1_val < CHUNK_SIDE_LENGTH; ++iter1_val) {
                            src_coord_3d[copy_config.plane_iter_axis1] = iter1_val;
                            for (int iter2_val = 0; iter2_val < CHUNK_SIDE_LENGTH; ++iter2_val) {
                                src_coord_3d[copy_config.plane_iter_axis2] = iter2_val;
                                // The 2D plane_data_ref is always indexed by [iter1_val * CHUNK_SIDE_LENGTH + iter2_val]
                                // regardless of which dimensions iter1 and iter2 represent.
                                plane_data_ref[iter1_val * CHUNK_SIDE_LENGTH + iter2_val] = 
                                    neighbor_full_block_data_ptr[Chunk::localToIndex(src_coord_3d.x, src_coord_3d.y, src_coord_3d.z)];
                                }
                            }
                        // End of data-driven plane copy
                    }
                );
            }
        } else {
            neighborCache[i].isGeneratedAndExists = false;
            // Defaults (isAllAir = true, blockData = std::nullopt) are fine.
        }
    }

    // 2. Perform locked read on the current chunk and generate its mesh
    currentChunk.performLockedRead(
        [&](const uint16_t* currentChunkBlocks_ptr, size_t capacity) {
            if (!currentChunkBlocks_ptr || capacity != CHUNK_VOLUME) {
                // This case should ideally be caught by the initial currentChunk.isAllAir() check,
                // but as a safeguard, if no valid block data is provided, we can't mesh.
                return; // meshData remains empty
            }

            // Iterate through blocks in the current chunk using nested loops
            for (int z = 0; z < CHUNK_SIDE_LENGTH; ++z) {
                for (int y = 0; y < CHUNK_SIDE_LENGTH; ++y) {
                    for (int x = 0; x < CHUNK_SIDE_LENGTH; ++x) {
                        size_t blockIndex = Chunk::localToIndex(x, y, z);
                        uint16_t blockID = currentChunkBlocks_ptr[blockIndex];

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
                            if (neighborLocalPosRelativeToCurrentChunk.x >= 0 && neighborLocalPosRelativeToCurrentChunk.x < CHUNK_SIDE_LENGTH &&
                                neighborLocalPosRelativeToCurrentChunk.y >= 0 && neighborLocalPosRelativeToCurrentChunk.y < CHUNK_SIDE_LENGTH &&
                                neighborLocalPosRelativeToCurrentChunk.z >= 0 && neighborLocalPosRelativeToCurrentChunk.z < CHUNK_SIDE_LENGTH) {
                                
                                size_t neighborIdxInCurrentChunk = Chunk::localToIndex(
                                    neighborLocalPosRelativeToCurrentChunk.x,
                                    neighborLocalPosRelativeToCurrentChunk.y,
                                    neighborLocalPosRelativeToCurrentChunk.z
                                );
                                neighborBlockID_val = currentChunkBlocks_ptr[neighborIdxInCurrentChunk];
                                
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
                                        // Access the cached boundary plane.
                                        // The coordinates x, y, z are for the current block in the current chunk.
                                        // These are used to index into the 2D plane.
                                        if (cachedNeighbor.boundaryPlaneData) { // Check if boundaryPlaneData has a value
                                            size_t plane_idx = 0;
                                            FaceDirection dir_of_this_face = static_cast<FaceDirection>(faceIndex); // This 'faceIndex' is the face of the current block
                                            const auto& access_config = Detail::PLANE_ACCESS_CONFIGS[static_cast<size_t>(dir_of_this_face)];

                                            int u_coord_val = currentBlockLocalPos_ivec[access_config.current_block_u_coord_component];
                                            int v_coord_val = currentBlockLocalPos_ivec[access_config.current_block_v_coord_component];
                                            plane_idx = static_cast<size_t>(u_coord_val * CHUNK_SIDE_LENGTH + v_coord_val);
                                            
                                            if (plane_idx < cachedNeighbor.boundaryPlaneData->size()) { // Bounds check for safety
                                                neighborBlockID_val = (*cachedNeighbor.boundaryPlaneData)[plane_idx];
                                            } else { // Should not happen with cubic chunks and correct indexing
                                                neighborBlockID_val = Blocks::AIR_ID; // Fallback
                                            }

                                            if (neighborBlockID_val == Blocks::AIR_ID) { 
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
                            }
                        } // End face check loop

                        if (anyCellFaceIsActuallyExposed) {
                            std::shared_ptr<const SeparableModelData> separableModelDataPtr = resourceManager.getModelForBlockType(blockID);
                            const Block* blockDef = blockRegistry.getBlockDefinition(blockID); 

                            if (separableModelDataPtr && blockDef) { 
                                // Construct the vec3 position only if we actually have model data to add
                                glm::vec3 currentBlockLocalPos_vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

                                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                                    if (cellFaceIsActuallyExposed[faceIdx]) { 
                                        const ModelData& faceGeom = separableModelDataPtr->canonicalFaces[faceIdx];
                                        // Only call addBlockModelToMeshData if faceGeom has vertices
                                        if (!faceGeom.vertices.empty()) {
                                            addBlockModelToMeshData(meshData, faceGeom, currentBlockLocalPos_vec3);
                                        }
                                    }
                                }
                                // Only call addBlockModelToMeshData if remainingGeometry has vertices
                                if (!separableModelDataPtr->remainingGeometry.vertices.empty()) {
                                    addBlockModelToMeshData(meshData, separableModelDataPtr->remainingGeometry, currentBlockLocalPos_vec3);
                                }
                            }
                        }
                    } // End x loop
                } // End y loop
            } // End z loop
        } // End lambda for currentChunk.performLockedRead
    ); // End call to currentChunk.performLockedRead

    return meshData;
}

} // namespace ChunkMesher