#include "Block.hpp"
#include <stdexcept> // For std::out_of_range

Block::Block(uint16_t id, const std::string& modelPath, const std::string& texturePath)
    : m_id(id), m_modelPath(modelPath), m_texturePath(texturePath), customShape(std::nullopt),
      m_fullOccludingFaces{} { // Initialize all faces to false (not full) by default
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

bool Block::hasFullOccludingFace(FaceDirection dir) const {
    size_t index = static_cast<size_t>(dir);
    if (index >= m_fullOccludingFaces.size()) {
        // This should ideally not happen if FaceDirection enum is used correctly
        throw std::out_of_range("FaceDirection out of bounds in hasFullOccludingFace");
    }
    return m_fullOccludingFaces[index];
}

void Block::setFullOccludingFace(FaceDirection dir, bool isFull) {
    size_t index = static_cast<size_t>(dir);
    if (index >= m_fullOccludingFaces.size()) {
        throw std::out_of_range("FaceDirection out of bounds in setFullOccludingFace");
    }
    m_fullOccludingFaces[index] = isFull;
}

FaceDirection Block::getOppositeFace(FaceDirection dir) {
    // This relies on the paired order of FaceDirection enum values (POS_X, NEG_X, POS_Y, NEG_Y, etc.)
    // e.g., POS_X (0) XOR 1 = NEG_X (1)
    //       NEG_X (1) XOR 1 = POS_X (0)
    return static_cast<FaceDirection>(static_cast<uint8_t>(dir) ^ 1);
}

void Block::setCustomShape(const Physics::VoxelShape& shape) {
    customShape.emplace(shape); // Use emplace to construct the VoxelShape in place
}

const std::optional<Physics::VoxelShape>& Block::getCustomShape() const {
    return customShape;
}