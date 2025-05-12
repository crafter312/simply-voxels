#include "World.hpp"
#include <random> // For random number generation

World::World() {
    // Initialize a random number generator
    std::random_device rd;  // Obtain a random number from hardware
    std::mt19937 gen(rd()); // Seed the generator

    // Define the range for coordinates (e.g., -5 to 5 for x, y, z)
    std::uniform_int_distribution<> coord_distrib(-5, 5);

    // Define the range for the number of blocks to generate (e.g., 8 to 15 blocks)
    std::uniform_int_distribution<> num_blocks_distrib(8, 15);

    int num_blocks_to_create = num_blocks_distrib(gen);

    // Generate random blocks
    for (int i = 0; i < num_blocks_to_create; ++i) {
        // Generate random integer coordinates, cast to float for glm::vec3
        float x = static_cast<float>(coord_distrib(gen));
        float y = static_cast<float>(coord_distrib(gen));
        float z = static_cast<float>(coord_distrib(gen));

        // Create a "dirt" block at the generated position
        // Note: This might place blocks at the same coordinates.
        // For initial testing, this is usually fine.
        blocks.push_back({glm::vec3(x, y, z), "dirt"});
    }
}

const std::vector<WorldBlock>& World::getBlocks() const {
    return blocks;
}