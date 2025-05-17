#ifndef BLOCK_DEFINITION_HPP // Renamed header guard for clarity
#define BLOCK_DEFINITION_HPP

#include <string>
#include <array>   // For std::array
#include <cstdint> // For uint16_t

// Enum to represent the 6 faces of a block/cell
// The order matches NEIGHBOR_OFFSETS in World.hpp for convenience
enum class FaceDirection : uint8_t {
    POS_X = 0, // +X
    NEG_X = 1, // -X
    POS_Y = 2, // +Y
    NEG_Y = 3, // -Y
    POS_Z = 4, // +Z
    NEG_Z = 5  // -Z
};

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

    bool hasFullOccludingFace(FaceDirection dir) const;
    void setFullOccludingFace(FaceDirection dir, bool isFull);

    // Helper to get the opposite face direction
    static FaceDirection getOppositeFace(FaceDirection dir);

private:
    uint16_t m_id;
    std::string m_modelPath;
    std::string m_texturePath;

    std::array<bool, 6> m_fullOccludingFaces; // True if the face in this direction is full and opaque
};

#endif // BLOCK_DEFINITION_HPP