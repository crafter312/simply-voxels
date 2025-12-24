#pragma once

#include <cstdint>

// Forward declaration
class Chunk;

/**
 * @brief Abstract interface for terrain generation strategies.
 */
class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;

    // Generates block data for the specific chunk using the provided world seed.
    // The implementation is responsible for filling the chunk's block array.
    virtual void generateChunk(Chunk& chunk, int64_t seed) = 0;
};