#ifndef PHYSICS_VOXELSHAPE_HPP
#define PHYSICS_VOXELSHAPE_HPP

#include "BoundingBox.hpp"

#include <glm/glm.hpp>
#include <vector>

// Add to Physics namespace in BoundingBox.hpp
namespace Physics {

// ... (BlockAABB struct from above) ...

struct VoxelShapeRayHit {
    bool isHit = false;
    float distance = -1.0f; // t-value along the ray from its origin
    glm::vec3 normal = glm::vec3(0.0f);   // Normal of the hit face on the specific AABB

    VoxelShapeRayHit() = default;
    VoxelShapeRayHit(float d, const glm::vec3& n) : isHit(true), distance(d), normal(n) {}
};


class VoxelShape {
public:
    std::vector<BlockAABB> aabbs; // The collection of AABBs forming this shape

    VoxelShape() = default; // Default constructor for an empty shape
    // Constructor from a single AABB
    explicit VoxelShape(const BlockAABB& singleAABB);
    // Constructor from a list of AABBs
    explicit VoxelShape(std::vector<BlockAABB> initialAABBs);

    bool isEmpty() const;

    // Equivalent to Block.createCuboidShape(...)
    // Creates a VoxelShape containing a single AABB.
    // Coordinates are 0-16 like Minecraft, then scaled to 0-1.
    static VoxelShape createCuboidShape(
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        float scale = 16.0f);

    // Equivalent to VoxelShapes.combineAndSimplify(...) with BooleanBiFunction.OR
    // For now, "simplify" will be a no-op or very basic.
    // A true CSG union and simplification is very complex.    
    using BooleanFunctionPtr = bool (*)(bool, bool); // Type alias for clarity
    static VoxelShape combineAndSimplify(
        const VoxelShape& shape1, 
        const VoxelShape& shape2, 
        BooleanFunctionPtr operation);

    // Updated signature for ray intersection test
    // Assumes rayOriginInBlockLocalSpace is the ray's origin relative to this VoxelShape's origin (0,0,0)
    VoxelShapeRayHit intersect(
        const glm::vec3& rayOriginInBlockLocalSpace, // Ray origin in this shape's local space
        const glm::vec3& rayDirection                // Normalized ray direction
    ) const;

private:
    // Simplification helpers
    static bool isAABBContained(const BlockAABB& inner, const BlockAABB& outer);
    static bool canMergeAABBs(const BlockAABB& a, const BlockAABB& b, BlockAABB& outMerged);
    static void removeSubsumedAABBs(std::vector<BlockAABB>& aabbs); // Changed to void, pass by ref
    static void mergeAdjacentAABBs(std::vector<BlockAABB>& aabbs);  // Changed to void, pass by ref

    // Helper for ray-AABB intersection
    static VoxelShapeRayHit intersectRayAABB_Helper(
        const glm::vec3& rayOriginLocal, // Ray origin in AABB's local space
        const glm::vec3& rayDirection,
        const glm::vec3& aabbMinLocal,   // AABB extents in its local space
        const glm::vec3& aabbMaxLocal    // AABB extents in its local space
    );
};

} // namespace Physics
#endif