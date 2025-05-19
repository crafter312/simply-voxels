#include "World.hpp"
#include <cmath>  // For std::floor
#include <iostream> // For debugging output (can be removed later)
#include <algorithm> // For std::max
#include <mutex> // For std::unique_lock
#include <optional> // For std::optional
#include <limits>   // For std::numeric_limits
#include <shared_mutex> // For std::shared_lock and std::unique_lock

//#define GLM_ENABLE_EXPERIMENTAL
#include "../Camera.hpp" // Include Camera definition
#include "../block/Blocks.hpp" // Include the centralized block definitions
#include "../resource/RegionManager.hpp" // Include RegionManager definition

World::World(std::shared_ptr<Camera> camera)
    : m_camera(camera) { // m_player will be initialized below
    m_regionManager = std::make_unique<WorldSave::RegionManager>("../run/regions/"); // Initialize RegionManager
    // Initially, enqueue chunks around the starting camera position (which is relative to the initial rebase origin 0,0,0)
    enqueueChunksNearCamera();
}

World::~World() {
    // The std::unique_ptr m_regionManager will be automatically destroyed here.
    // Before it's destroyed, we should save any modified chunks.
    std::cout << "World destructor: Attempting to save modified chunks..." << std::endl;

    if (m_regionManager) {
        // A shared_lock is appropriate here because we are only iterating through m_chunks
        // and calling methods on the Chunk objects. We are not modifying the m_chunks map structure itself.
        // The saveChunkToFile method takes a Chunk&, implying it might modify the chunk (e.g., clear its dirty flag).
        // In a destructor, we assume other threads that might interact with these specific chunk objects
        // have been shut down or are no longer active.
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex);

        std::cout << "Iterating over " << m_chunks.size() << " loaded chunks to save modifications." << std::endl;
        int savedCount = 0;
        int isDirtyCount = 0;

        for (auto& pair : m_chunks) { // Use auto& to get Chunk& for pair.second
            Chunk& chunk = pair.second; // chunk is Chunk&
            if (chunk.isDirty()) { // Check if the chunk has unpersisted data changes
                isDirtyCount++;
                if (m_regionManager->saveChunkToFile(chunk)) {
                    savedCount++;
                } else {
                    // Outputting coordinates directly to avoid needing glm::to_string and potential include issues.
                    std::cerr << "Warning: Failed to save chunk at coordinates ("
                              << pair.first.x << ", " << pair.first.y << ", " << pair.first.z
                              << ") during shutdown." << std::endl;
                }
            }
        }
        std::cout << "Found " << isDirtyCount << " chunks requiring save. Successfully saved " << savedCount << " chunks." << std::endl;
    } else {
        std::cout << "RegionManager is null, skipping chunk saving on shutdown." << std::endl;
    }
    std::cout << "World destruction complete." << std::endl;
    // m_regionManager (and other members) will be destroyed automatically after this.
}

std::optional<glm::ivec3> World::worldToChunkCoordinates(glm::i64vec3 worldPosition) {
    // Perform floored division using integer arithmetic to avoid float precision issues.
    // CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH are positive.
    int64_t chunkX_i64 = worldPosition.x / CHUNK_WIDTH;
    int64_t chunkY_i64 = worldPosition.y / CHUNK_HEIGHT;
    int64_t chunkZ_i64 = worldPosition.z / CHUNK_DEPTH;

    // Adjust for negative coordinates if there's a non-zero remainder,
    // because integer division truncates towards zero.
    if (worldPosition.x < 0 && (worldPosition.x % CHUNK_WIDTH != 0)) {
        chunkX_i64--;
    }
    if (worldPosition.y < 0 && (worldPosition.y % CHUNK_HEIGHT != 0)) {
        chunkY_i64--;
    }
    if (worldPosition.z < 0 && (worldPosition.z % CHUNK_DEPTH != 0)) {
        chunkZ_i64--;
    }

    // Check if the 64-bit chunk coordinates fit into 32-bit integers
    if (chunkX_i64 < std::numeric_limits<int>::min() || chunkX_i64 > std::numeric_limits<int>::max() ||
        chunkY_i64 < std::numeric_limits<int>::min() || chunkY_i64 > std::numeric_limits<int>::max() ||
        chunkZ_i64 < std::numeric_limits<int>::min() || chunkZ_i64 > std::numeric_limits<int>::max()) {
        // One or more chunk coordinates are out of the 32-bit range
        return std::nullopt;
    }

    return glm::ivec3( // Safely cast to int and return
        static_cast<int>(chunkX_i64),
        static_cast<int>(chunkY_i64),
        static_cast<int>(chunkZ_i64)
    );

}

std::optional<glm::ivec3> World::worldToLocalCoordinates(glm::i64vec3 worldPosition, glm::ivec3 chunkCoord) {
    glm::i64vec3 localPos_i64;
    localPos_i64.x = worldPosition.x - static_cast<int64_t>(chunkCoord.x) * CHUNK_WIDTH;
    localPos_i64.y = worldPosition.y - static_cast<int64_t>(chunkCoord.y) * CHUNK_HEIGHT;
    localPos_i64.z = worldPosition.z - static_cast<int64_t>(chunkCoord.z) * CHUNK_DEPTH;

    // Check if the calculated local coordinates are within the valid range [0, CHUNK_DIMENSION - 1].
    // Mathematically, if chunkCoord was correctly derived from worldToChunkCoordinates(worldPosition),
    // these local coordinates should always be in range.
    // This check adds robustness if the function is called with inconsistent inputs,
    // or helps catch unexpected behavior.
    if (localPos_i64.x < 0 || localPos_i64.x >= CHUNK_WIDTH ||
        localPos_i64.y < 0 || localPos_i64.y >= CHUNK_HEIGHT ||
        localPos_i64.z < 0 || localPos_i64.z >= CHUNK_DEPTH) {
        // This indicates that the provided chunkCoord was not the canonical one for worldPosition,
        // or an unexpected calculation error occurred.
        // std::cerr << "Warning: worldToLocalCoordinates produced out-of-bounds local coordinates. "
        //           << "worldPos: (" << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z << "), "
        //           << "chunkCoord: (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "), "
        //           << "calculated local_i64: (" << localPos_i64.x << "," << localPos_i64.y << "," << localPos_i64.z << ")" << std::endl;
        return std::nullopt;
    }

    return glm::ivec3(
        static_cast<int>(localPos_i64.x),
        static_cast<int>(localPos_i64.y),
        static_cast<int>(localPos_i64.z)
    );
}

uint16_t World::getBlockID(glm::i64vec3 worldPosition) const {
    std::optional<glm::ivec3> optChunkCoord = worldToChunkCoordinates(worldPosition);
    if (!optChunkCoord) {
        // World position results in chunk coordinates outside the 32-bit integer range.
        return Blocks::AIR_ID;
    }
    glm::ivec3 chunkCoord = *optChunkCoord;

    { // Scope for shared_lock
        std::shared_lock<std::shared_mutex> lock(m_chunks_mutex); // Read lock
        auto it = m_chunks.find(chunkCoord);
        if (it != m_chunks.end()) {
            const Chunk& chunk = it->second;
            if (!chunk.isGenerated()) {
                return Blocks::AIR_ID; // Chunk exists but not generated, treat as air
            }
            std::optional<glm::ivec3> optLocalPos = worldToLocalCoordinates(worldPosition, chunkCoord);
            if (!optLocalPos) {
                // This case should ideally not be reached if worldToChunkCoordinates is correct
                // and its result is used here. It implies a deeper logic error.
                std::cerr << "Critical Error: In getBlockID, worldToLocalCoordinates failed for worldPos ("
                          << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
                          << ") and chunkCoord ("
                          << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                          << "). This indicates an internal logic flaw." << std::endl;
                return Blocks::AIR_ID;
            }
            glm::ivec3 localPos = *optLocalPos;
            return chunk.getBlock(localPos.x, localPos.y, localPos.z);
        }
    }
    return Blocks::AIR_ID; // Chunk doesn't exist, so it's air. Using Blocks::AIR_ID for consistency.
}

void World::setBlockID(glm::i64vec3 worldPosition, uint16_t blockID) {
    std::optional<glm::ivec3> optChunkCoord = worldToChunkCoordinates(worldPosition);
    if (!optChunkCoord) {
        // World position results in chunk coordinates outside the 32-bit integer range.
        // Cannot set block here.
        // Optionally, log a warning:
        // std::cerr << "Warning: Attempted to set block at a position "
        //           << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
        //           << " which is outside the 32-bit chunk coordinate addressable range." << std::endl;
        return;
    }
    glm::ivec3 chunkCoord = *optChunkCoord;

    // getOrCreateChunk handles its own unique lock for potential modification of m_chunks
    Chunk& chunk = getOrCreateChunk(chunkCoord);
    std::optional<glm::ivec3> optLocalPos = worldToLocalCoordinates(worldPosition, chunkCoord);
    if (!optLocalPos) {
        // Similar to getBlockID, this indicates a critical internal logic error.
        std::cerr << "Critical Error: In setBlockID, worldToLocalCoordinates failed for worldPos ("
                  << worldPosition.x << "," << worldPosition.y << "," << worldPosition.z
                  << ") and chunkCoord ("
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                  << "). Block will not be set. This indicates an internal logic flaw." << std::endl;
        return;
    }
    glm::ivec3 localPos = *optLocalPos;
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

            // Attempt to load from file first
            bool loadedFromFile = m_regionManager->loadChunkFromFile(newChunk);
            if (loadedFromFile) {
                // std::cout << "Loaded chunk " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << " from file." << std::endl;
                // loadChunkFromFile already calls markGenerated() and sets dirty to false.
            } else {
                newChunk.generate(); // Generate terrain if not loaded from file
                // std::cout << "Generated chunk: " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << std::endl;
            }

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
        
        // Operations on m_chunks (saving, reading for neighbors, then erasing) need to be atomic
        // with respect to other accesses to m_chunks. A unique_lock is appropriate here.
        {
            std::unique_lock<std::shared_mutex> lock(m_chunks_mutex); // Write lock for erase and consistent neighbor check
            
            auto it = m_chunks.find(chunkCoord);
            if (it != m_chunks.end()) {
                Chunk& chunk_to_unload = it->second;

                // Save the chunk if it needs saving before unloading
                if (chunk_to_unload.isDirty() && !m_regionManager->saveChunkToFile(chunk_to_unload)) {
                    std::cerr << "Warning: Failed to save chunk " << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << " during unload." << std::endl;
                }

                // Before erasing the chunk, mark its 6 direct neighbors as dirty.
                // This allows them to rebuild their meshes now that this chunk will be gone (effectively air).
                for (size_t i = 0; i < NUM_NEIGHBORS; ++i) {
                    glm::ivec3 neighborCoord = chunkCoord + NEIGHBOR_OFFSETS[i];
                    if (m_chunks.count(neighborCoord)) { // Check if the neighbor exists
                        m_changedChunks.insert(neighborCoord);
                    }
                }
                m_changedChunks.insert(chunkCoord); // Mark this chunk as changed (it's being removed)
                m_chunks.erase(it); // Erase using the iterator
            }
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