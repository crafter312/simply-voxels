#ifndef CHUNK_HPP
#define CHUNK_HPP

#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::ivec3
#include <array>
#include <glm/gtc/noise.hpp> // For glm::perlin
#include <vector>
#include <iostream> // For std::cout in logging
#include <atomic>  // For std::atomic
#include <cstdint> // For uint16_t
#include <memory>  // For std::unique_ptr
#include <shared_mutex> // For std::shared_mutex
#include <functional> // For std::function

// Vulkan forward declarations (actual resources will be added later)
struct VkBuffer_T;
struct VkDeviceMemory_T;
typedef VkBuffer_T* VkBuffer;
typedef VkDeviceMemory_T* VkDeviceMemory;


constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_HEIGHT = 16;
constexpr int CHUNK_DEPTH = 16;
constexpr int CHUNK_VOLUME = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH; // 4096

// Terrain generation parameters
constexpr double TERRAIN_FREQUENCY = 0.01; // Controls the "zoom" of the noise. Smaller = larger features.
constexpr double TERRAIN_AMPLITUDE = 20.0; // Controls the height variation of the terrain.
constexpr int DIRT_LAYER_THICKNESS = 3;   // Number of dirt blocks on top of stone.

class Chunk {
public:
    Chunk(glm::ivec3 chunkCoord); // Parameter now represents chunk coordinates
    ~Chunk();

    // Disable copy constructor and copy assignment operator to prevent shallow copies
    // of m_blocks. If copying is needed, a proper deep copy mechanism should be implemented.
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // Allow move constructor and move assignment operator
    Chunk(Chunk&&) noexcept;
    Chunk& operator=(Chunk&&) noexcept;

    uint16_t getBlock(int localX, int localY, int localZ) const;
    void setBlock(int localX, int localY, int localZ, uint16_t blockID);

    glm::ivec3 getWorldPosition() const;
    bool isDirty() const;
    void setDirty(bool dirty);
    bool isAllAir() const;
    // Methods for thread-safe data access for meshing
    glm::ivec3 getChunkCoord() const { return m_chunkCoord; }
    void generate(); // New method for procedural generation
    bool isGenerated() const { 
        bool genStatus = m_isGenerated.load(std::memory_order_acquire); 
        // std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] isGenerated() called, returning: " << genStatus << std::endl; // Can be very verbose
        return genStatus; 
    } // Check if initial generation is complete
    void markGenerated() { 
        //std::cout << "Chunk [" << m_chunkCoord.x << "," << m_chunkCoord.y << "," << m_chunkCoord.z << "] markGenerated() called." << std::endl;
        m_isGenerated.store(true, std::memory_order_release); }   // Called at the end of generate()

    // Methods for RegionManager to load data
    void setAllBlocks(const std::array<uint16_t, CHUNK_VOLUME>& new_blocks_data);
    void setAllAir();
    // For RegionManager to track if this chunk instance was loaded from a file
    void setWasLoadedFromFile(bool val) { m_wasLoadedFromFile = val; }
    bool wasLoadedFromFile() const { return m_wasLoadedFromFile; }

    // Executes the provided 'writer' function while holding a lock on the chunk's data.
    // The 'writer' is given a pointer to the internal block buffer and its capacity in elements.
    // This ensures that operations like direct decompression happen atomically with respect to other chunk data access.
    // The writer is responsible for filling the buffer correctly.
    // After this function, the caller should typically also call markGenerated(), setWasLoadedFromFile(true), and setDirty(false).
    void performLockedWrite(std::function<void(uint16_t* block_data_buffer, size_t buffer_capacity_elements)> writer_action);

    // Executes the provided 'reader' function while holding a shared lock on the chunk's data.
    // The 'reader' is given a const pointer to the internal block buffer (or nullptr if all_air/unallocated)
    // and its capacity in elements.
    void performLockedRead(std::function<void(const uint16_t* block_data_buffer, size_t buffer_capacity_elements)> reader_action) const;

    // Helper to convert 3D local coordinates to a 1D array index
    // Made public static so it can be used by external functions like ChunkMesher
    static inline size_t localToIndex(int x, int y, int z) {
        // Add bounds checking or assertions if desired
        // if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_DEPTH) {
        //     throw std::out_of_range("Block coordinates are out of chunk bounds");
        // }
        return static_cast<size_t>(x + y * CHUNK_WIDTH + z * CHUNK_WIDTH * CHUNK_HEIGHT);
    }

private:
    glm::ivec3 m_chunkCoord; // Integer grid coordinates of the chunk (e.g., (0,0,0), (1,0,0))

    // Using unique_ptr for automatic memory management and to easily represent
    // the "not allocated" state for all-air chunks.
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> m_blocks;

    bool m_isAllAir; // True if the chunk contains only air blocks (m_blocks will be nullptr)
    bool m_isDirty;  // True if the chunk's geometry needs to be rebuilt (for rendering)
    std::atomic<bool> m_isGenerated; // True if the chunk has completed its initial procedural generation
    mutable std::shared_mutex m_data_mutex; // To protect m_blocks, m_isAllAir, and m_isDirty
    bool m_wasLoadedFromFile = false; // True if this chunk instance's data was populated by RegionManager::loadChunkFromFile

    void allocateBlockStorage();

};

#endif // CHUNK_HPP