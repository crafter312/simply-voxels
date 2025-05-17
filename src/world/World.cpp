#include "World.hpp"
#include <cmath>  // For std::floor
#include <iostream> // For debugging output (can be removed later)
#include <algorithm> // For std::max
#include <mutex> // For std::unique_lock
#include <shared_mutex> // For std::shared_lock and std::unique_lock

//#define GLM_ENABLE_EXPERIMENTAL
//#include <glm/gtx/string_cast.hpp> // For glm::to_string
#include "../Camera.hpp" // Include Camera definition
#include "../block/Blocks.hpp" // Include the centralized block definitions

World::World(std::shared_ptr<Camera> camera)
    : m_camera(camera) {
    
    // Initially, enqueue chunks around the starting camera position (which is relative to the initial rebase origin 0,0,0)
    enqueueChunksNearCamera();
}

glm::ivec3 World::worldToChunkCoordinates(glm::ivec3 worldPosition) { 
    return glm::ivec3(
        static_cast<int>(std::floor(static_cast<float>(worldPosition.x) / CHUNK_WIDTH)),
        static_cast<int>(std::floor(static_cast<float>(worldPosition.y) / CHUNK_HEIGHT)),
        static_cast<int>(std::floor(static_cast<float>(worldPosition.z) / CHUNK_DEPTH))
    );
}

glm::ivec3 World::worldToLocalCoordinates(glm::ivec3 worldPosition, glm::ivec3 chunkCoord) {
    return glm::ivec3(
        worldPosition.x - chunkCoord.x * CHUNK_WIDTH,
        worldPosition.y - chunkCoord.y * CHUNK_HEIGHT,
        worldPosition.z - chunkCoord.z * CHUNK_DEPTH
    );
}

uint16_t World::getBlockID(glm::ivec3 worldPosition) const {
    glm::ivec3 chunkCoord = worldToChunkCoordinates(worldPosition);
    // std::cout << "World::getBlockID: Query for worldPos [" << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z << "] -> chunkCoord [" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "]" << std::endl;
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        const Chunk& chunk = it->second;
        bool genStatus = chunk.isGenerated(); // Call it once
        // std::cout << "World::getBlockID: Chunk [" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "] FOUND. isGenerated: " << genStatus << std::endl;
        if (!genStatus) { // If the queried chunk hasn't finished its generation
            // std::cout << "World::getBlockID: Chunk [" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "] NOT YET GENERATED. Returning AIR." << std::endl;
            return Blocks::AIR_ID;    // Treat it as air to avoid using incomplete data
        }
        glm::ivec3 localPos = worldToLocalCoordinates(worldPosition, chunkCoord);
        uint16_t blockId = chunk.getBlock(localPos.x, localPos.y, localPos.z);
        // std::cout << "World::getBlockID: Chunk [" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "] GENERATED. LocalPos [" << localPos.x << "," << localPos.y << "," << localPos.z << "] -> BlockID: " << blockId << std::endl;
        return blockId;
    }
    // std::cout << "World::getBlockID: Chunk [" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "] NOT FOUND. Returning AIR." << std::endl;
    return Blocks::AIR_ID; // Chunk doesn't exist, so it's air. Using Blocks::AIR_ID for consistency.
}

void World::setBlockID(glm::ivec3 worldPosition, uint16_t blockID) {
    glm::ivec3 chunkCoord = worldToChunkCoordinates(worldPosition);
    // getOrCreateChunk handles its own unique lock for potential modification of m_chunks
    Chunk& chunk = getOrCreateChunk(chunkCoord);
    glm::ivec3 localPos = worldToLocalCoordinates(worldPosition, chunkCoord);
    chunk.setBlock(localPos.x, localPos.y, localPos.z, blockID); // This will call markModified on the chunk

    // If the modified block is on a boundary, mark the adjacent neighbor chunk(s) as dirty too.
    // This ensures the neighbor rebuilds its mesh considering the change.
    // The neighbor checks need a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for neighbor checks
        if (localPos.x == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[1]; // -X
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.x == CHUNK_WIDTH - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[0]; // +X
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
        if (localPos.y == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[3]; // -Y
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.y == CHUNK_HEIGHT - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[2]; // +Y
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
        if (localPos.z == 0) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[5]; // -Z
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        } else if (localPos.z == CHUNK_DEPTH - 1) {
            glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[4]; // +Z
            if (m_chunks.count(neighborCoord)) m_changedChunks.insert(neighborCoord);
        }
    }
    m_changedChunks.insert(chunkCoord); // Mark this chunk as changed
}

Chunk* World::getChunk(glm::ivec3 chunkCoord) {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

const Chunk* World::getChunk(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    auto it = m_chunks.find(chunkCoord);
    if (it != m_chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

Chunk& World::getOrCreateChunk(glm::ivec3 chunkCoord) {
    std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Write lock for potential emplace
    auto it = m_chunks.find(chunkCoord);
    if (it == m_chunks.end()) {
        // Chunk does not exist, create it and emplace it into the map
        // std::piecewise_construct allows constructing key and value in-place
        it = m_chunks.emplace(std::piecewise_construct, 
                              std::forward_as_tuple(chunkCoord),  // Arguments for glm::ivec3 key
                              std::forward_as_tuple(chunkCoord)).first; // Arguments for Chunk constructor
        m_changedChunks.insert(chunkCoord); // Mark newly created chunk as changed (needs a mesh)
    }
    return it->second;
}

bool World::isChunkLoaded(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
    return m_chunks.count(chunkCoord) > 0;
}

const std::map<glm::ivec3, Chunk, IVec3Comparator>& World::getChunkMap() const {
    // Note: Returning a reference to the map itself is tricky for thread safety
    // if the caller iterates it without holding a lock.
    // For now, assume callers (like VulkanRenderer::drawFrame) are on the main thread
    // or handle locking externally if this method is used by other threads.
    // A shared_lock here would only protect the immediate return, not subsequent use.
    return m_chunks;
}

void World::update(float deltaTime) {
    m_rebaseOccurredThisFrame = false; // Reset flag at the start of update
    checkAndRebase(); // Check and perform rebase first
    enqueueChunksNearCamera();
    enqueueChunksToUnload();
    processLoadQueue();
    processUnloadQueue();
}

void World::enqueueChunksNearCamera() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    // Iterate over a cubic bounding box that encompasses the sphere
    glm::ivec3 minChunk = cameraChunkPos - glm::ivec3(LOAD_CHUNK_RADIUS);
    glm::ivec3 maxChunk = cameraChunkPos + glm::ivec3(LOAD_CHUNK_RADIUS);

    float loadRadiusSquared = static_cast<float>(LOAD_CHUNK_RADIUS * LOAD_CHUNK_RADIUS);

    // The m_chunks.find() needs a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        for (int x = minChunk.x; x <= maxChunk.x; ++x) {
            for (int y = minChunk.y; y <= maxChunk.y; ++y) {
                for (int z = minChunk.z; z <= maxChunk.z; ++z) {
                    glm::ivec3 chunkCoord(x, y, z);
                    // Check Euclidean distance for spherical loading
                    glm::ivec3 diff = chunkCoord - cameraChunkPos;
                    float distSq = static_cast<float>(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

                    if (distSq <= loadRadiusSquared) {
                        if (m_chunks.find(chunkCoord) == m_chunks.end()) m_loadQueue.push(chunkCoord);
                    }
              }
          }
        }
    }
}

void World::enqueueChunksToUnload() {
    if (!m_camera) return;

    // Get camera's absolute chunk position
    glm::ivec3 cameraChunkPos = m_camera->getAbsoluteChunkPos();

    float unloadRadiusSquared = static_cast<float>(UNLOAD_CHUNK_RADIUS * UNLOAD_CHUNK_RADIUS);

    // Iterate through existing chunks and unload those outside the radius
    // Iterating m_chunks needs a read lock.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
            glm::ivec3 chunkCoord = it->first;
            glm::ivec3 diff = chunkCoord - cameraChunkPos;
            float distSq = static_cast<float>(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

            // If chunk is outside the spherical unload radius
            if (distSq > unloadRadiusSquared) {
                m_unloadQueue.push(chunkCoord);
                // std::cout << "Enqueueing chunk to unload: " << glm::to_string(chunkCoord) << std::endl;
            }
        }
    }
}


void World::processLoadQueue() {
    int loadedCount = 0;

    while (!m_loadQueue.empty() && loadedCount < MAX_CHUNKS_TO_LOAD_PER_FRAME) {
        glm::ivec3 chunkCoord = m_loadQueue.front();
        m_loadQueue.pop();

        // Check if the chunk is already loaded (shouldn't be, but double-check)
        bool needsLoading;
        {
            std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for find
            needsLoading = (m_chunks.find(chunkCoord) == m_chunks.end());
        }

        if (needsLoading) {
            // getOrCreateChunk handles its own unique lock for potential emplace
            Chunk& newChunk = getOrCreateChunk(chunkCoord);
            newChunk.generate(); // Generate terrain for the new chunk (this also marks it dirty via constructor/setBlock)
            // std::cout << "Loaded chunk: " << glm::to_string(chunkCoord) << std::endl;

            // Mark its 6 direct neighbors as dirty so they can update their meshes
            // relative to this newly generated and loaded chunk.
            { // Scope for neighbor check lock
                std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for neighbor checks
                for (size_t i = 0; i < NUM_NEIGHBORS; ++i) {
                    glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
                    if (m_chunks.count(neighborCoord)) { // Check if the neighbor exists (is loaded)
                        m_changedChunks.insert(neighborCoord);
                    }
                }
            }

            loadedCount++; 
            // m_changedChunks is already updated by getOrCreateChunk if it's new
        }
    }
}

void World::processUnloadQueue() {
    while (!m_unloadQueue.empty()) {
        glm::ivec3 chunkCoord = m_unloadQueue.front();
        m_unloadQueue.pop();
        
        // Operations on m_chunks (reading for neighbors, then erasing) need to be atomic
        // with respect to other accesses to m_chunks.
        {
            std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Write lock for erase and consistent neighbor check
            // Before erasing the chunk, mark its 6 direct neighbors as dirty.
            // This allows them to rebuild their meshes now that this chunk will be gone (effectively air).
            for (size_t i = 0; i < NUM_NEIGHBORS; ++i) {
                glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
                if (m_chunks.count(neighborCoord)) { // Check if the neighbor exists
                    m_changedChunks.insert(neighborCoord);
                }
            }
            // std::cout << "Unloading chunk: " << glm::to_string(chunkCoord) << std::endl;
            m_changedChunks.insert(chunkCoord); // Mark this chunk as changed (it's being removed)
            m_chunks.erase(chunkCoord);
        }
    }
}

const std::set<glm::ivec3, IVec3Comparator>& World::getChangedChunks() const {
    return m_changedChunks;
}
void World::acknowledgeChunkChangeProcessed(const glm::ivec3& chunkCoord) {
    m_changedChunks.erase(chunkCoord);
}

void World::markAllChunksDirty() {
    // This function is now for general "all chunks need mesh update" scenarios,
    // not specifically for rebase model matrix updates.
    {
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock for iterating m_chunks
        for (const auto& pair : m_chunks) {
            m_changedChunks.insert(pair.first); // Add to regular changed chunks for mesh processing
        }
    }
    std::cout << "Marked all " << m_chunks.size() << " chunks as dirty." << std::endl;
}

void World::checkAndRebase() {
    if (!m_camera) return;

    // Get camera's absolute chunk position directly
    glm::ivec3 cameraCurrentChunkPos = m_camera->getAbsoluteChunkPos();

    // Calculate distance from the current rebase origin chunk
    glm::ivec3 diff = cameraCurrentChunkPos - m_rebaseOriginChunkCoord;
    int max_dist = std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)});

    if (max_dist > REBASE_TRIGGER_RADIUS_CHUNKS) {
        glm::ivec3 oldRebaseOriginChunkCoord = m_rebaseOriginChunkCoord;
        m_rebaseOriginChunkCoord = cameraCurrentChunkPos; // New origin is the chunk the camera is in
        m_rebaseOccurredThisFrame = true; // Set the flag
        // std::cout << "Rebasing origin from " << glm::to_string(oldRebaseOriginChunkCoord) << " to " << glm::to_string(m_rebaseOriginChunkCoord) << std::endl;
        // No longer calling markAllChunksDirty() here for rebase.
        // The renderer will detect m_rebaseOccurredThisFrame.
    }
}

bool World::rebaseOccurredLastFrame() const {
    return m_rebaseOccurredThisFrame;
}

World::ChunkGenStatus World::getChunkGeneratedStatus(glm::ivec3 chunkCoord) const {
    std::shared_lock<std::shared_mutex> lock(m_chunks_mutex);
    auto it = m_chunks.find(chunkCoord);
    if (it == m_chunks.end()) {
        return ChunkGenStatus::NOT_FOUND;
    }
    // Chunk exists, check its generation status
    // The isGenerated() method on Chunk is atomic and safe to call here.
    if (!it->second.isGenerated()) {
        return ChunkGenStatus::LOADED_NOT_GENERATED;
    }
    return ChunkGenStatus::LOADED_AND_GENERATED;
}