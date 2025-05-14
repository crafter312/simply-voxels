#ifndef CHUNK_HPP
#define CHUNK_HPP

#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp> // For glm::ivec3
#include <array>
#include <vector>
#include <cstdint> // For uint16_t
#include <memory>  // For std::unique_ptr

// Vulkan forward declarations (actual resources will be added later)
struct VkBuffer_T;
struct VkDeviceMemory_T;
typedef VkBuffer_T* VkBuffer;
typedef VkDeviceMemory_T* VkDeviceMemory;


constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_HEIGHT = 16;
constexpr int CHUNK_DEPTH = 16;
constexpr int CHUNK_VOLUME = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH; // 4096

const uint16_t AIR_BLOCK_ID = 0; // Define a constant for air

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

    // Placeholder for Vulkan resources (to be implemented later)
    // VkBuffer m_vertexBuffer = nullptr; // VK_NULL_HANDLE
    // VkDeviceMemory m_vertexBufferMemory = nullptr; // VK_NULL_HANDLE
    // VkBuffer m_indexBuffer = nullptr; // VK_NULL_HANDLE
    // VkDeviceMemory m_indexBufferMemory = nullptr; // VK_NULL_HANDLE
    // VkDescriptorSet m_descriptorSet = nullptr; // VK_NULL_HANDLE

private:
    glm::ivec3 m_chunkCoord; // Integer grid coordinates of the chunk (e.g., (0,0,0), (1,0,0))

    // Using unique_ptr for automatic memory management and to easily represent
    // the "not allocated" state for all-air chunks.
    std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> m_blocks;

    bool m_isAllAir; // True if the chunk contains only air blocks (m_blocks will be nullptr)
    bool m_isDirty;  // True if the chunk's geometry needs to be rebuilt

    // Helper to convert 3D local coordinates to a 1D array index
    static inline size_t localToIndex(int x, int y, int z) {
        // Add bounds checking or assertions if desired
        // if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_DEPTH) {
        //     throw std::out_of_range("Block coordinates are out of chunk bounds");
        // }
        return static_cast<size_t>(x + y * CHUNK_WIDTH + z * CHUNK_WIDTH * CHUNK_HEIGHT);
    }

    void allocateBlockStorage();
};

#endif // CHUNK_HPP