#ifndef RAYCASTER_HPP
#define RAYCASTER_HPP

#include <glm/glm.hpp>
// #include <optional> // Not used with the current RaycastResult struct design

// Forward declarations
class World; 
class BlockRegistry;

struct RaycastResult {
    bool hit = false;
    glm::ivec3 blockPosition; // World coordinates of the hit block
    glm::ivec3 hitNormal;     // Normal of the face that was hit (e.g., {0,1,0} for top face)
    float distance = 0.0f;

    // Constructor for no-hit
    RaycastResult() = default;

    // Constructor for a hit
    RaycastResult(const glm::ivec3& pos, const glm::ivec3& normal, float dist)
        : hit(true), blockPosition(pos), hitNormal(normal), distance(dist) {}
};

constexpr float MAX_RAY_DISTANCE = 8.0f; // Player's reach for interaction

namespace VoxelRaycaster {
    RaycastResult castRay(
        const World& world,          // To query block IDs
        const BlockRegistry& blockRegistry, // To query block type properties
        glm::ivec3 originChunkPos,    // Absolute chunk coordinate of the ray's origin
        glm::vec3 originLocalPos,     // Ray's origin position within its chunk [0, CHUNK_DIMENSION)
        glm::vec3 rayDirection       // Normalized direction vector
    );
} // namespace VoxelRaycaster

#endif // RAYCASTER_HPP