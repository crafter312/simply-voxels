#ifndef BLOCK_DEFINITION_HPP // Renamed header guard for clarity
#define BLOCK_DEFINITION_HPP

#include <string>
#include <cstdint> // For uint16_t

/**
 * @class Block
 * @brief Defines the properties of a specific type of block.
 *
 * This class holds the ID, and paths to the model and texture files
 * that define the appearance of a block type.
 * This is distinct from the Block struct in World.hpp, which represents
 * an instance of a block at a certain position.
 */
class Block {
public:
    Block(uint16_t id, const std::string& modelPath, const std::string& texturePath);

    uint16_t getID() const;
    const std::string& getModelPath() const;
    const std::string& getTexturePath() const;

private:
    uint16_t m_id;
    std::string m_modelPath;
    std::string m_texturePath;
};

#endif // BLOCK_DEFINITION_HPP