#include "Block.hpp"

Block::Block(uint16_t id, const std::string& modelPath, const std::string& texturePath)
    : m_id(id), m_modelPath(modelPath), m_texturePath(texturePath) {
    // Constructor initializes member variables
}

uint16_t Block::getID() const {
    return m_id;
}

const std::string& Block::getModelPath() const {
    return m_modelPath;
}

const std::string& Block::getTexturePath() const {
    return m_texturePath;
}