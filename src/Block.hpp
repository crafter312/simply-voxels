#ifndef BLOCK_DEFINITION_HPP // Renamed header guard for clarity
#define BLOCK_DEFINITION_HPP

#include <string>

/**
 * @class Block
 * @brief Defines the properties of a specific type of block.
 *
 * This class holds the type name, and paths to the model and texture files
 * that define the appearance of a block type (e.g., "dirt", "stone").
 * This is distinct from the Block struct in World.hpp, which represents
 * an instance of a block at a certain position.
 */
class Block {
public:
    Block(const std::string& typeName, const std::string& modelPath, const std::string& texturePath);

    const std::string& getTypeName() const;
    const std::string& getModelPath() const;
    const std::string& getTexturePath() const;

private:
    std::string m_typeName;
    std::string m_modelPath;
    std::string m_texturePath;
};

#endif // BLOCK_DEFINITION_HPP