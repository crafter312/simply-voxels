#include <cstdint>
#include <cmath>

#ifndef GLM_FORCE_PURE
#define GLM_FORCE_PURE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#if defined(__wasm__)
    #define WASM_EXPORT __attribute__((visibility("default")))
#else
    #define WASM_EXPORT
#endif

constexpr int CHUNK_DIM = 16;
constexpr int CHUNK_VOLUME = CHUNK_DIM * CHUNK_DIM * CHUNK_DIM;

constexpr int SEED_COORD_SCALE = 10000;

constexpr double SINE_FREQ = 0.05;
constexpr double SINE_AMP = 8.0;

constexpr double NOISE_FREQ = 0.1;
constexpr double NOISE_AMP = 3.0;

// Base height of the terrain (e.g. Y=32)
constexpr int BASE_HEIGHT = 32;

constexpr int DIRT_LAYER_THICKNESS = 3;

extern "C" {
    // Signature matches WasmTerrainGenerator: void generate(i32, i32, i32, i64, i32)
    WASM_EXPORT
    void generate(int32_t chunkX, int32_t chunkY, int32_t chunkZ, int64_t worldSeed, uint16_t* outputBuffer) {

        /******** Pre-calculate a perlin noise offset from the world seed ********/

        // 1. Split the 64-bit world seed into two 32-bit integer components
        // This ensures 0 seed results in (0, 0) offset, and different seeds produce different offsets
        const int32_t gridX = static_cast<int32_t>(worldSeed >> 32);
        const int32_t gridY = static_cast<int32_t>(worldSeed & 0xFFFFFFFF);

        // 2. Scale these components to get a larger offset, then convert to double and scale by the frequency
        // This prevents terrain similarities across different seeds, while also properly preparing to add to
        // chunk coordinate such that precision loss is minimized.
        glm::dvec2 seedOffsetPerlin(
            static_cast<double>(static_cast<int64_t>(gridX) * SEED_COORD_SCALE) * NOISE_FREQ,
            static_cast<double>(static_cast<int64_t>(gridY) * SEED_COORD_SCALE) * NOISE_FREQ
        );

        /******** Pre-calculate the chunk's origin contribution to the sine/Perlin input ********/

        // 1. This uses 64-bit integers for the multiplication to prevent overflow, then converts
        // the result to double. This is safe as the result fits within a double's integer precision.
        glm::dvec2 chunkOriginSineInput(
            static_cast<double>(static_cast<int64_t>(chunkX) * CHUNK_DIM) * SINE_FREQ,
            static_cast<double>(static_cast<int64_t>(chunkZ) * CHUNK_DIM) * SINE_FREQ
        );
        glm::dvec2 chunkOriginPerlinInput(
            static_cast<double>(static_cast<int64_t>(chunkX) * CHUNK_DIM) * NOISE_FREQ,
            static_cast<double>(static_cast<int64_t>(chunkZ) * CHUNK_DIM) * NOISE_FREQ
        );

        // 2. Add the seed offset to get the final chunk origin Perlin input
        chunkOriginPerlinInput += seedOffsetPerlin;

        // Also get the absolute Y origin of the chunk using safe 64-bit arithmetic.
        const int64_t chunkWorldOriginY = static_cast<int64_t>(chunkY) * CHUNK_DIM;

        for (int x = 0; x < CHUNK_DIM; x++) {
            for (int z = 0; z < CHUNK_DIM; z++) {
                
                // Calculate local coordinate offsets
                glm::dvec2 sineInput = chunkOriginSineInput + glm::dvec2(static_cast<double>(x) * SINE_FREQ, static_cast<double>(z) * SINE_FREQ);
                glm::dvec2 perlinInput = chunkOriginPerlinInput + glm::dvec2(static_cast<double>(x) * NOISE_FREQ, static_cast<double>(z) * NOISE_FREQ);

                // 1. C++ Standard Library sin() - 2D Sinusoidal wave (egg-carton shape)
                double sineVal = std::sin(sineInput.x) + std::sin(sineInput.y);
                double sineHeight = sineVal * SINE_AMP * 0.5f;

                // 2. GLM Perlin Noise - Small amplitude detail
                float noiseHeight = glm::perlin(perlinInput) * NOISE_AMP;

                // Combine heights
                int surfaceHeight = static_cast<int>(BASE_HEIGHT + sineHeight + noiseHeight);

                for (int y = 0; y < CHUNK_DIM; y++) {
                    int64_t currentBlockAbsoluteY = chunkWorldOriginY + y;
                    uint16_t blockID;
                    
                    // Indexing: x + (y * width) + (z * width * height)
                    int index = x + (y * CHUNK_DIM) + (z * CHUNK_DIM * CHUNK_DIM);

                    if (currentBlockAbsoluteY < surfaceHeight) {
                        if (currentBlockAbsoluteY >= surfaceHeight - DIRT_LAYER_THICKNESS) {
                            outputBuffer[index] = 1; // Dirt
                        } else {
                            outputBuffer[index] = 2; // Stone
                        }
                    } else {
                        outputBuffer[index] = 0; // Air
                    }
                }
            }
        }
    }
}