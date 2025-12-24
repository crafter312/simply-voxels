#pragma once

#include "ITerrainGenerator.hpp"

/**
 * @brief A standard CPU-based Perlin noise terrain generator.
 * This replicates the original hardcoded generation logic from Chunk.cpp.
 */
class SimplePerlinGenerator : public ITerrainGenerator {
public:
    void generateChunk(Chunk& chunk, int64_t seed) override;
};