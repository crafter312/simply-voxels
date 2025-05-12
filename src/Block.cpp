#include "Block.hpp"

Block::Block(const std::string& typeName, const std::string& modelPath, const std::string& texturePath)
    : m_typeName(typeName), m_modelPath(modelPath), m_texturePath(texturePath) {
    // Constructor initializes member variables
}

const std::string& Block::getTypeName() const {
    return m_typeName;
}

const std::string& Block::getModelPath() const {
    return m_modelPath;
}

const std::string& Block::getTexturePath() const {
    return m_texturePath;
}