#ifndef WORLD_HPP
#define WORLD_HPP

#include <map>
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::ivec3
#include <memory> // For std::shared_ptr
#include <set>    // To track changed chunks efficiently
#include <queue> // For chunk loading/unloading queue
#include "Chunk.hpp" // Include the Chunk definition

// Forward declaration for Camera
class Camera;

// Comparator for glm::ivec3 to use it as a key in std::map
struct IVec3Comparator {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

// Class to manage all blocks in the world
class World {
public:
    // Constructor now accepts a shared pointer to the Camera
    World(std::shared_ptr<Camera> camera);

    // Destructor (optional, but good practice)
    // Gets the block ID at the given world position.
    // Returns AIR_BLOCK_ID if the chunk doesn't exist or the block is air.
    uint16_t getBlockID(glm::ivec3 worldPosition) const;

    // Sets the block ID at the given world position.
    // This may involve creating a new chunk if one doesn't exist at that location.
    void setBlockID(glm::ivec3 worldPosition, uint16_t blockID);

    // Retrieves a pointer to a chunk at the given chunk coordinates.
    // Returns nullptr if the chunk does not exist.
    Chunk* getChunk(glm::ivec3 chunkCoord);
    const Chunk* getChunk(glm::ivec3 chunkCoord) const;

    // Retrieves a reference to a chunk at the given chunk coordinates.
    // If the chunk does not exist, it is created.
    Chunk& getOrCreateChunk(glm::ivec3 chunkCoord);

    // Helper to convert world coordinates to chunk coordinates
    static glm::ivec3 worldToChunkCoordinates(glm::ivec3 worldPosition);
    // Helper to convert world coordinates to local block coordinates within a chunk
    static glm::ivec3 worldToLocalCoordinates(glm::ivec3 worldPosition, glm::ivec3 chunkCoord);

    // Provides access to the underlying chunk map for iteration (e.g., by the renderer)
    const std::map<glm::ivec3, Chunk, IVec3Comparator>& getChunkMap() const;

    // Call this periodically to process loading/unloading
    void update(float deltaTime);

    // Get the current rebase origin in chunk coordinates
    glm::ivec3 getRebaseOriginChunkCoord() const { return m_rebaseOriginChunkCoord; }

    const std::set<glm::ivec3, IVec3Comparator>& getChangedChunks() const; // IVec3Comparator for std::set
    void clearChangedChunks();
    void markAllChunksDirty(); // Mark all loaded chunks for mesh regeneration (e.g., texture change)
    bool rebaseOccurredLastFrame() const; // Check if a rebase happened

private:
    // --- Constants for chunk management ---
    static constexpr int LOAD_CHUNK_RADIUS = 2; // Radius in chunks around the camera to load chunks
    static constexpr int REBASE_TRIGGER_RADIUS_CHUNKS = 8; // Radius in chunks from rebase origin to trigger a rebase
    static constexpr int UNLOAD_CHUNK_RADIUS = 3; // Radius in chunks beyond which to unload chunks
    static constexpr int MAX_CHUNKS_TO_LOAD_PER_FRAME = 2; // Max chunks to process from load queue per frame

    std::map<glm::ivec3, Chunk, IVec3Comparator> m_chunks;
    std::shared_ptr<Camera> m_camera; // Store a pointer to the camera    
    std::set<glm::ivec3, IVec3Comparator> m_changedChunks; // Set of chunk coordinates that have been modified

    glm::ivec3 m_rebaseOriginChunkCoord = glm::ivec3(0,0,0); // The current origin for rendering
    bool m_rebaseOccurredThisFrame = false; // Flag to indicate a rebase happened in the current update
    // Queues for loading and unloading chunks.  They hold chunk coordinates.
    std::queue<glm::ivec3> m_loadQueue;
    std::queue<glm::ivec3> m_unloadQueue;

    // Internal methods for managing chunk loading/unloading
    void enqueueChunksNearCamera();
    void enqueueChunksToUnload();
    void processLoadQueue();
    void processUnloadQueue();
    void checkAndRebase(); // Check if rebase is needed and perform it
};

#endif // WORLD_HPP