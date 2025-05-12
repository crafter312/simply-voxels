#ifndef WORLD_HPP
#define WORLD_HPP

#include <vector>
#include <string>
#include <glm/vec3.hpp> // For glm::vec3

// Struct to hold information about a single block instance in the world
struct WorldBlock {
    glm::vec3 position; // Integer coordinates stored as floats
    std::string type;   // Type of the block, e.g., "dirt", "stone"
};

// Class to manage all blocks in the world
class World {
public:
    World(); // Constructor to initialize the world with some blocks
    const std::vector<WorldBlock>& getBlocks() const; // Getter for the list of blocks

private:
    std::vector<WorldBlock> blocks; // Collection of all blocks in the world
};

#endif // WORLD_HPP