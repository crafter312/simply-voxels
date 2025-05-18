#ifndef BOUNDING_BOX_HPP
#define BOUNDING_BOX_HPP

#include <glm/glm.hpp>
#include <vector>
#include <optional>

namespace Physics {

// Represents a single AABB in local block coordinates (typically 0.0 to 1.0 range)
struct BlockAABB {
    glm::vec3 minExtents; // e.g., {0.0f, 0.0f, 0.0f}
    glm::vec3 maxExtents; // e.g., {1.0f, 0.5f, 1.0f} for a half-slab

    // Default constructor, initializes to zero or an empty AABB
    BlockAABB() : minExtents(0.0f), maxExtents(0.0f) {}

    BlockAABB(const glm::vec3& min, const glm::vec3& max) : minExtents(min), maxExtents(max) {}
};

} // namespace Physics
#endif // BOUNDING_BOX_HPP