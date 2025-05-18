#include "VoxelShape.hpp"
#include "../util/BooleanOperation.hpp" // For BooleanBiFunction::* functions
#include <algorithm> // For std::min, std::max, std::sort, std::unique
#include <utility>   // For std::move
#include <vector>
#include <iostream>  // For warnings on unimplemented features
#include <limits>    // For std::numeric_limits

namespace Physics {

// Constructor from a single AABB
VoxelShape::VoxelShape(const BlockAABB& singleAABB) : aabbs({singleAABB}) {}

// Constructor from a list of AABBs
VoxelShape::VoxelShape(std::vector<BlockAABB> initialAABBs) : aabbs(std::move(initialAABBs)) {}

bool VoxelShape::isEmpty() const {
    return aabbs.empty();
}

// Equivalent to Block.createCuboidShape(...)
// Creates a VoxelShape containing a single AABB.
// Coordinates are 0-16 like Minecraft, then scaled to 0-1.
VoxelShape VoxelShape::createCuboidShape(
    float x1, float y1, float z1,
    float x2, float y2, float z2,
    float scale) {

    // Ensure min and max are correctly ordered for each axis
    glm::vec3 minExtents(
        std::min(x1, x2) / scale,
        std::min(y1, y2) / scale,
        std::min(z1, z2) / scale
    );
    glm::vec3 maxExtents(
        std::max(x1, x2) / scale,
        std::max(y1, y2) / scale,
        std::max(z1, z2) / scale
    );
    return VoxelShape(BlockAABB(minExtents, maxExtents));
}

// --- Private Static Simplification Helpers ---

bool VoxelShape::isAABBContained(const BlockAABB& inner, const BlockAABB& outer) {
    return outer.minExtents.x <= inner.minExtents.x &&
           outer.minExtents.y <= inner.minExtents.y &&
           outer.minExtents.z <= inner.minExtents.z &&
           inner.maxExtents.x <= outer.maxExtents.x &&
           inner.maxExtents.y <= outer.maxExtents.y &&
           inner.maxExtents.z <= outer.maxExtents.z;
}

bool VoxelShape::canMergeAABBs(const BlockAABB& a, const BlockAABB& b, BlockAABB& outMerged) {
    // Try merge along X
    if (a.minExtents.y == b.minExtents.y && a.maxExtents.y == b.maxExtents.y &&
        a.minExtents.z == b.minExtents.z && a.maxExtents.z == b.maxExtents.z) {
        if (a.maxExtents.x == b.minExtents.x) { // a is to the left of b
            outMerged = BlockAABB(a.minExtents, glm::vec3(b.maxExtents.x, a.maxExtents.y, a.maxExtents.z));
            return true;
        }
        if (b.maxExtents.x == a.minExtents.x) { // b is to the left of a
            outMerged = BlockAABB(b.minExtents, glm::vec3(a.maxExtents.x, b.maxExtents.y, b.maxExtents.z));
            return true;
        }
    }
    // Try merge along Y
    if (a.minExtents.x == b.minExtents.x && a.maxExtents.x == b.maxExtents.x &&
        a.minExtents.z == b.minExtents.z && a.maxExtents.z == b.maxExtents.z) {
        if (a.maxExtents.y == b.minExtents.y) {
            outMerged = BlockAABB(a.minExtents, glm::vec3(a.maxExtents.x, b.maxExtents.y, a.maxExtents.z));
            return true;
        }
        if (b.maxExtents.y == a.minExtents.y) {
            outMerged = BlockAABB(b.minExtents, glm::vec3(b.maxExtents.x, a.maxExtents.y, b.maxExtents.z));
            return true;
        }
    }
    // Try merge along Z
    if (a.minExtents.x == b.minExtents.x && a.maxExtents.x == b.maxExtents.x &&
        a.minExtents.y == b.minExtents.y && a.maxExtents.y == b.maxExtents.y) {
        if (a.maxExtents.z == b.minExtents.z) {
            outMerged = BlockAABB(a.minExtents, glm::vec3(a.maxExtents.x, a.maxExtents.y, b.maxExtents.z));
            return true;
        }
        if (b.maxExtents.z == a.minExtents.z) {
            outMerged = BlockAABB(b.minExtents, glm::vec3(b.maxExtents.x, b.maxExtents.y, a.maxExtents.z));
            return true;
        }
    }
    return false;
}

void VoxelShape::removeSubsumedAABBs(std::vector<BlockAABB>& aabbs) {
    if (aabbs.size() < 2) return;

    std::vector<bool> subsumed_flags(aabbs.size(), false);
    for (size_t i = 0; i < aabbs.size(); ++i) {
        if (subsumed_flags[i]) continue;
        for (size_t j = 0; j < aabbs.size(); ++j) {
            if (i == j || subsumed_flags[j]) continue;

            if (isAABBContained(aabbs[j], aabbs[i])) { // j is contained in i
                subsumed_flags[j] = true;
            } else if (isAABBContained(aabbs[i], aabbs[j])) { // i is contained in j
                subsumed_flags[i] = true;
                break; // aabbs[i] is subsumed, no need for it to check against others
            }
        }
    }

    // Erase-remove idiom style to modify in-place
    size_t current_write_idx = 0;
    for (size_t i = 0; i < aabbs.size(); ++i) {
        if (!subsumed_flags[i]) {
            if (current_write_idx != i) {
                // Move if we are compacting the vector
                aabbs[current_write_idx] = std::move(aabbs[i]);
            }
            current_write_idx++;
        }
    }
    aabbs.resize(current_write_idx);
}

void VoxelShape::mergeAdjacentAABBs(std::vector<BlockAABB>& aabbs) {
    if (aabbs.size() < 2) return;

    bool mergedInOuterPass;
    do {
        mergedInOuterPass = false;
        if (aabbs.size() < 2) break;

        std::vector<BlockAABB> currentPassResult;
        currentPassResult.reserve(aabbs.size()); // Pre-allocate to avoid multiple reallocations
        std::vector<bool> processed(aabbs.size(), false);

        for (size_t i = 0; i < aabbs.size(); ++i) {
            if (processed[i]) continue;

            BlockAABB accumulator = aabbs[i];
            processed[i] = true; 

            bool accumulatorChanged;
            do {
                accumulatorChanged = false;
                for (size_t j = 0; j < aabbs.size(); ++j) {
                    if (processed[j]) continue; 

                    BlockAABB tempMergedBox;
                    if (canMergeAABBs(accumulator, aabbs[j], tempMergedBox)) {
                        accumulator = tempMergedBox;
                        processed[j] = true; 
                        mergedInOuterPass = true;
                        accumulatorChanged = true; 
                        // No break, try to merge the new accumulator with all other unprocessed boxes
                    }
                }
            } while (accumulatorChanged);
            currentPassResult.push_back(accumulator);
        }
        // Efficiently replace contents of aabbs with currentPassResult
        aabbs.swap(currentPassResult); 
    } while (mergedInOuterPass);
}

// Equivalent to VoxelShapes.combineAndSimplify(...) with BooleanBiFunction.OR
// For now, "simplify" will be a no-op or very basic.
// A true CSG union and simplification is very complex.
VoxelShape VoxelShape::combineAndSimplify(const VoxelShape& shape1, const VoxelShape& shape2, BooleanFunctionPtr operation) {    
    // Handle cases where one or both shapes are empty
    // operation(inS1, inS2)
    if (shape1.isEmpty() && shape2.isEmpty()) {
        return operation(false, false) ? VoxelShape(BlockAABB(glm::vec3(0), glm::vec3(0))) : VoxelShape(); // Or some representation of "all true" if needed
    }
    if (shape1.isEmpty()) { // shape1 is false, shape2 might be true
        return operation(false, true) ? shape2 : VoxelShape();
    }
    if (shape2.isEmpty()) { // shape1 might be true, shape2 is false
        return operation(true, false) ? shape1 : VoxelShape();
    }

    std::vector<BlockAABB> workingAABBs;

    if (operation == BooleanBiFunction::OR) {
        workingAABBs = shape1.aabbs; // Start with all AABBs from the first shape
        // Add all AABBs from the second shape
        workingAABBs.insert(workingAABBs.end(), shape2.aabbs.begin(), shape2.aabbs.end());
        // Further simplification would involve merging overlapping AABBs.
    } else if (operation == BooleanBiFunction::AND) {
        for (const auto& aabb1 : shape1.aabbs) {
            for (const auto& aabb2 : shape2.aabbs) {
                // Calculate intersection of aabb1 and aabb2
                glm::vec3 min_intersect = glm::max(aabb1.minExtents, aabb2.minExtents);
                glm::vec3 max_intersect = glm::min(aabb1.maxExtents, aabb2.maxExtents);

                // Check if intersection is valid (has positive volume)
                if (min_intersect.x < max_intersect.x &&
                    min_intersect.y < max_intersect.y &&
                    min_intersect.z < max_intersect.z) {
                    workingAABBs.emplace_back(min_intersect, max_intersect);
                }
            }
        }
        // Further simplification would involve merging or removing redundant intersection AABBs.
    } else if (operation == BooleanBiFunction::ONLY_FIRST) { // A and not B (A \ B)
        // This is complex as it requires subtracting volumes, which can split AABBs.
        // A full implementation would iterate through each aabbA in shape1,
        // and for each aabbB in shape2, subtract aabbB from aabbA.
        // The subtraction itself can result in multiple new AABBs.
        std::cerr << "Warning: CSG operation ONLY_FIRST (A \\ B) is not fully implemented and will return an empty shape. True AABB subtraction is required.\n";
        // workingAABBs = shape1.aabbs; // Incorrect, but a placeholder
        // For now, returning empty as a safe default for an unimplemented complex operation.
    } else if (operation == BooleanBiFunction::ONLY_SECOND) { // B and not A (B \ A)
        std::cerr << "Warning: CSG operation ONLY_SECOND (B \\ A) is not fully implemented and will return an empty shape. True AABB subtraction is required.\n";
    }
    // Add more 'else if' for other specific BooleanBiFunction functions if you
    // find direct AABB manipulation strategies for them.
    else {
        // For truly generic operations, a common (but computationally intensive) approach
        // is to discretize the space defined by all AABB boundaries into minimal cells.
        // Then, for each cell, determine if its center is in shape1 and shape2,
        // apply the boolean operation, and if true, add the cell to the result.
        // This is effectively voxelizing the CSG operation.
        std::cerr << "Warning: This specific CSG operation is not implemented via direct AABB manipulation and will return an empty shape.\n";
    }

    // Apply more advanced simplification for specific operations
    if (operation == BooleanBiFunction::AND) {
        // For AND, we first remove fully contained (subsumed) AABBs,
        // then merge adjacent ones.
        removeSubsumedAABBs(workingAABBs);
        mergeAdjacentAABBs(workingAABBs);
    } else if (operation == BooleanBiFunction::OR) {
        // For OR, the same simplification steps can be beneficial.
        removeSubsumedAABBs(workingAABBs);
        mergeAdjacentAABBs(workingAABBs);
    }

    // Final generic pass: Sort and remove exact duplicates.
    if (!workingAABBs.empty()) {
        std::sort(workingAABBs.begin(), workingAABBs.end(), 
            [](const BlockAABB& a, const BlockAABB& b) {
                if (a.minExtents.x != b.minExtents.x) return a.minExtents.x < b.minExtents.x;
                if (a.minExtents.y != b.minExtents.y) return a.minExtents.y < b.minExtents.y;
                if (a.minExtents.z != b.minExtents.z) return a.minExtents.z < b.minExtents.z;
                if (a.maxExtents.x != b.maxExtents.x) return a.maxExtents.x < b.maxExtents.x;
                if (a.maxExtents.y != b.maxExtents.y) return a.maxExtents.y < b.maxExtents.y;
                return a.maxExtents.z < b.maxExtents.z;
            });
        workingAABBs.erase(
            std::unique(workingAABBs.begin(), workingAABBs.end(),
                [](const BlockAABB& a, const BlockAABB& b) {
                    return glm::all(glm::equal(a.minExtents, b.minExtents)) && 
                           glm::all(glm::equal(a.maxExtents, b.maxExtents));
                }),
            workingAABBs.end());
    }
    
    return VoxelShape(std::move(workingAABBs));
}

// --- Ray Intersection ---

VoxelShapeRayHit VoxelShape::intersectRayAABB_Helper(
    const glm::vec3& rayOriginLocal, const glm::vec3& rayDirection,
    const glm::vec3& aabbMinLocal, const glm::vec3& aabbMaxLocal) {

    // Slab method for Ray-AABB intersection (operating in local coordinates)
    // Calculate 1.0f / rayDirection components, watching for division by zero
    glm::vec3 invDir = glm::vec3(
        rayDirection.x == 0.0f ? std::numeric_limits<float>::infinity() : 1.0f / rayDirection.x,
        rayDirection.y == 0.0f ? std::numeric_limits<float>::infinity() : 1.0f / rayDirection.y,
        rayDirection.z == 0.0f ? std::numeric_limits<float>::infinity() : 1.0f / rayDirection.z
    );

    glm::vec3 t1 = (aabbMinLocal - rayOriginLocal) * invDir;
    glm::vec3 t2 = (aabbMaxLocal - rayOriginLocal) * invDir;

    glm::vec3 tMinXYZ = glm::min(t1, t2);
    glm::vec3 tMaxXYZ = glm::max(t1, t2);

    float tNear = glm::max(glm::max(tMinXYZ.x, tMinXYZ.y), tMinXYZ.z);
    float tFar = glm::min(glm::min(tMaxXYZ.x, tMaxXYZ.y), tMaxXYZ.z);

    if (tNear > tFar || tFar < 0.0f) { // No intersection or intersection is behind the ray
        return VoxelShapeRayHit(); // Default (isHit = false)
    }

    // If tNear is negative, the ray starts inside the AABB.
    // For block interaction, we usually want the exit point if starting inside,
    // or the entry point if starting outside.
    // If tNear < 0, the actual intersection distance we care about for "entry" is tFar (if tFar > 0).
    // However, for simplicity and typical use (ray starting outside), tNear is the entry.
    // If the ray starts inside (tNear < 0), and we want the *first* positive intersection,
    // we should use tFar if tNear is negative. But if tNear is positive, it's the entry.
    // For now, let's assume we want the first positive t value.
    float hitDistance = tNear;
    if (tNear < 0.0f) { // Ray origin is inside the AABB
        // We are interested in the exit point if we consider "hitting" from inside.
        // Or, if we only care about entry from outside, this case might be ignored or handled differently.
        // For now, let's take the exit point tFar as the distance if tNear is negative.
        // This means if the ray starts inside, it hits the "back" face.
        // If tFar is also negative, then the AABB is entirely behind the ray.
        hitDistance = tFar; 
        if (hitDistance < 0.0f) return VoxelShapeRayHit(); // AABB is entirely behind
    }

    // Calculate hit normal
    glm::vec3 hitNormal(0.0f);
    // Check which plane was hit by comparing tNear to the tMin components
    // Add a small epsilon for floating point comparisons
    constexpr float epsilon = 1e-5f; 
    if (std::abs(tNear - tMinXYZ.x) < epsilon) {
        hitNormal.x = (rayDirection.x > 0) ? -1.0f : 1.0f;
    } else if (std::abs(tNear - tMinXYZ.y) < epsilon) {
        hitNormal.y = (rayDirection.y > 0) ? -1.0f : 1.0f;
    } else if (std::abs(tNear - tMinXYZ.z) < epsilon) {
        hitNormal.z = (rayDirection.z > 0) ? -1.0f : 1.0f;
    }
    // If ray started inside, the normal should be flipped because we are hitting from inside.
    if (tNear < 0.0f) {
        // If we used tFar, the normal calculation needs to be based on tFar and tMaxXYZ
        // This part is tricky. For now, let's simplify: if tNear < 0, the normal is more complex.
        // For typical block breaking, ray starts outside.
        // Let's stick to the normal for tNear > 0. If tNear < 0, the normal might be less intuitive
        // with this simple logic. A robust solution for rays starting inside might require more care.
        // For now, if tNear < 0, we are "exiting", so the normal should be outward.
        // The current normal logic is for entry.
        // TODO: Refine normal calculation if ray starts inside AABB.
        // For now, if tNear < 0, we'll use the normal as if hitting from outside, then flip it.
        // This is a simplification.
        // A more correct way for exit normal: check which of tMaxXYZ components corresponds to tFar.
        hitNormal = -hitNormal; // Crude flip if starting inside.
    }

    return VoxelShapeRayHit(hitDistance, glm::normalize(hitNormal)); // Ensure normal is normalized if not already unit
}

VoxelShapeRayHit VoxelShape::intersect(
    const glm::vec3& rayOriginInBlockLocalSpace,
    const glm::vec3& rayDirection) const {

    VoxelShapeRayHit closestHit; // Defaults to isHit = false, distance = -1

    for (const auto& localAABB : aabbs) {
        // Both ray and AABB are in the same local space of this VoxelShape
        VoxelShapeRayHit currentAABBHit = intersectRayAABB_Helper(
            rayOriginInBlockLocalSpace,
            rayDirection,
            localAABB.minExtents, // Already in local space
            localAABB.maxExtents  // Already in local space
        );

        if (currentAABBHit.isHit) {
            if (!closestHit.isHit || currentAABBHit.distance < closestHit.distance) {
                closestHit = currentAABBHit;
            }
        }
    }
    return closestHit;
}

} // namespace Physics