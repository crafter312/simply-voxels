#include "Chunk.hpp"
#include <stdexcept> // For std::out_of_range (if strict bounds checking is added)
#include <glm/gtc/noise.hpp> // For glm::perlin (already in hpp but good for explicitness)
#include <iostream>  // For debugging (optional)
#include "../block/Blocks.hpp" // For centralized block ID definitions

Chunk::Chunk(glm::ivec3 chunkCoord)
    : m_chunkCoord(chunkCoord),
      m_blocks(nullptr), // Start with no block data allocated
      m_isAllAir(true),  // Assume all air initially
      m_isDirty(true)    // New chunks are dirty by default, needing a mesh
{
    // std::cout << "Chunk created at chunk coordinates: " << m_chunkCoord.x << ", " << m_chunkCoord.y << ", " << m_chunkCoord.z << std::endl;
}

Chunk::~Chunk() {
    // std::unique_ptr will automatically handle deallocation of m_blocks if it was allocated.
    // glm::ivec3 worldPos = getWorldPosition(); // Calculate before potential destruction output
    // std::cout << "Chunk destroyed at world coordinates: " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << " (Chunk Coords: " << m_chunkCoord.x << ", " << m_chunkCoord.y << ", " << m_chunkCoord.z << ")" << std::endl;
}

// Move constructor
Chunk::Chunk(Chunk&& other) noexcept
    : m_chunkCoord(other.m_chunkCoord),
      m_blocks(std::move(other.m_blocks)), // Transfer ownership of the block data
      m_isAllAir(other.m_isAllAir),
      m_isDirty(other.m_isDirty)
{
    // Reset other's state to a valid, empty state
    other.m_isAllAir = true;
    other.m_isDirty = true; // Or false, depending on desired state after move
}

// Move assignment operator
Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this != &other) {
        m_chunkCoord = other.m_chunkCoord;
        m_blocks = std::move(other.m_blocks); // Transfer ownership
        m_isAllAir = other.m_isAllAir;
        m_isDirty = other.m_isDirty;

        // Reset other's state
        other.m_isAllAir = true;
        other.m_isDirty = true;
    }
    return *this;
}

void Chunk::allocateBlockStorage() {
    if (!m_blocks) { // Only allocate if not already allocated
        m_blocks = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>();
        m_blocks->fill(Blocks::AIR_ID); // Initialize all new blocks to air
        m_isAllAir = false; // No longer all air once storage is allocated
    }
}

uint16_t Chunk::getBlock(int localX, int localY, int localZ) const {
    if (localX < 0 || localX >= CHUNK_WIDTH ||
        localY < 0 || localY >= CHUNK_HEIGHT ||
        localZ < 0 || localZ >= CHUNK_DEPTH) {
        // Potentially handle out-of-bounds access, e.g., by returning AIR_BLOCK_ID
        // or throwing an error. For now, let's assume valid local coordinates
        // or rely on the caller to ensure this.
        // For world-level getBlock, this would query neighbor chunks.        
        return Blocks::AIR_ID; // Or throw
    }

    if (m_isAllAir || !m_blocks) {
        return Blocks::AIR_ID;
    }
    return (*m_blocks)[localToIndex(localX, localY, localZ)];
}

void Chunk::setBlock(int localX, int localY, int localZ, uint16_t blockID) {
    if (localX < 0 || localX >= CHUNK_WIDTH ||
        localY < 0 || localY >= CHUNK_HEIGHT ||
        localZ < 0 || localZ >= CHUNK_DEPTH) {
        // Cannot set a block outside the chunk's bounds from within the chunk itself.
        // This should be handled by a world manager.
        // For now, we can ignore or log an error.
        std::cerr << "Attempted to set block outside of chunk bounds!" << std::endl;
        return;
    }

    if (m_isAllAir && blockID == Blocks::AIR_ID) {
        // Setting air in an all-air chunk, no change needed.
        return;
    }

    if (m_isAllAir && blockID != Blocks::AIR_ID) {
        // First non-air block being placed in an all-air chunk.
        allocateBlockStorage(); // This also sets m_isAllAir to false.
    }
    // By this point, if blockID is non-air, m_blocks is guaranteed to be allocated.
    // If blockID is air, and m_blocks was already allocated, we just set it.

    if (m_blocks) { // Should always be true if we are setting a non-air block or were not all-air
        (*m_blocks)[localToIndex(localX, localY, localZ)] = blockID;
    }
    
    // If we set a block (even to air in a non-all-air chunk), the chunk is dirty.
    m_isDirty = true;

    // Future optimization: If blockID is AIR_BLOCK_ID, check if the chunk became all-air.
    // If so, m_blocks.reset(); m_isAllAir = true;
}

glm::ivec3 Chunk::getWorldPosition() const {
    return glm::ivec3(m_chunkCoord.x * CHUNK_WIDTH,
                      m_chunkCoord.y * CHUNK_HEIGHT,
                      m_chunkCoord.z * CHUNK_DEPTH);
}
bool Chunk::isDirty() const { return m_isDirty; }
void Chunk::setDirty(bool dirty) { m_isDirty = dirty; }
bool Chunk::isAllAir() const { return m_isAllAir; }

std::unique_ptr<std::array<uint16_t, CHUNK_VOLUME>> Chunk::getBlockDataSnapshot() const {
    if (m_isAllAir || !m_blocks) {
        return nullptr; // Or return an empty (all-air) snapshot if preferred by mesher
    }
    // Create a copy of the block data
    auto snapshot = std::make_unique<std::array<uint16_t, CHUNK_VOLUME>>(*m_blocks);
    return snapshot;
}

void Chunk::generate() {
    // Terrain generation parameters
    const int baseSurfaceAbsoluteY = CHUNK_HEIGHT / 2; // An arbitrary "sea level" or average ground height in absolute Y.
                                                // Adjust this if your world's "ground" is typically higher or lower.
                                                // For chunks at y=0, this means surface is around local y=8.

    glm::ivec3 chunkWorldOrigin = getWorldPosition(); // Absolute world coordinates of this chunk's origin (0,0,0 local)

    // No need to call allocateBlockStorage() explicitly at the start.
    // setBlock() will call it if a non-air block is placed in an all-air chunk.
    // m_isAllAir is true by default for a new chunk.

    for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            // Calculate absolute world X and Z for the current block column
            // Using double for precision with Perlin noise, especially with large coordinates
            double absoluteWorldX = static_cast<double>(chunkWorldOrigin.x + lx);
            double absoluteWorldZ = static_cast<double>(chunkWorldOrigin.z + lz);

            // Generate Perlin noise value. glm::perlin for dvec2 returns double in [-1, 1]
            double noiseValue = glm::perlin(glm::dvec2(absoluteWorldX * TERRAIN_FREQUENCY, absoluteWorldZ * TERRAIN_FREQUENCY));

            // Calculate surface height for this column
            // This is an absolute Y coordinate in the world
            int surfaceTopAbsoluteY = baseSurfaceAbsoluteY + static_cast<int>(noiseValue * TERRAIN_AMPLITUDE);

            for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
                int currentBlockAbsoluteY = chunkWorldOrigin.y + ly; // Absolute Y of the current block layer

                if (currentBlockAbsoluteY < surfaceTopAbsoluteY) {
                    setBlock(lx, ly, lz, Blocks::STONE_ID);
                } else {
                    setBlock(lx, ly, lz, Blocks::AIR_ID);
                }
            }
        }
    }

    // After all blocks are set, accurately determine if the chunk is all air.
    // This is important because setBlock(AIR_BLOCK_ID) in an all-air chunk doesn't allocate.
    // If non-air blocks were placed, m_blocks would be allocated.
    // If it was allocated then filled with air, we should deallocate.
    bool finalIsAllAir = true;
    if (m_blocks) { // Only check if m_blocks was ever allocated
        for (const auto& blockID : *m_blocks) {
            if (blockID != Blocks::AIR_ID) {
                finalIsAllAir = false;
                break;
            }
        }
        if (finalIsAllAir) {
            m_blocks.reset(); // Deallocate if it ended up all air
        }
    }
    m_isAllAir = finalIsAllAir; // Set the final state
    m_isDirty = true; // Mark the chunk as dirty so its mesh will be rebuilt.
}