#include "Raycaster.hpp"
#include "../world/World.hpp" // For World::getBlockID and potentially Blocks::AIR_ID
#include "../block/Block.hpp" // Assuming Blocks::AIR_ID is defined here or accessible via World.hpp
#include "../world/Chunk.hpp" // For CHUNK_WIDTH, CHUNK_HEIGHT, CHUNK_DEPTH
#include "../block/BlockRegistry.hpp" // For BlockRegistry
#include "../physics/VoxelShape.hpp" // For Physics::VoxelShape and Physics::VoxelShapeRayHit
#include "../block/Blocks.hpp"

#include <cmath>   // For std::floor, std::abs
#include <limits>  // For std::numeric_limits

namespace VoxelRaycaster {

RaycastResult castRay(
    const World& world,
    const BlockRegistry& blockRegistry,
    glm::ivec3 originChunkPos,
    glm::vec3 originLocalPos,
    glm::vec3 rayDirection) { // Assumed to be normalized

    // Calculate the absolute integer coordinates of the starting voxel.
    // This combines the large-scale chunk position (integer) with the integer part of the local position.
    // Use int64_t for intermediate calculations to prevent overflow when originChunkPos is large.
    // We now use glm::i64vec3 for currentBlockPos to allow traversal beyond int32_t limits.
    // This is the initial voxel the ray starts in.
    glm::i64vec3 initialRayVoxelPos_i64; 
    initialRayVoxelPos_i64.x = 
        static_cast<int64_t>(originChunkPos.x) * CHUNK_WIDTH +
        static_cast<int64_t>(std::floor(originLocalPos.x));
    initialRayVoxelPos_i64.y = 
        static_cast<int64_t>(originChunkPos.y) * CHUNK_HEIGHT +
        static_cast<int64_t>(std::floor(originLocalPos.y));
    initialRayVoxelPos_i64.z = 
        static_cast<int64_t>(originChunkPos.z) * CHUNK_DEPTH +
        static_cast<int64_t>(std::floor(originLocalPos.z));

    // If the ray starts outside the int32_t addressable world, we could potentially return early.
    // However, the DDA loop will naturally handle this by not finding blocks if it queries outside.
    // For now, we let it proceed. The check will happen before calling getBlockID.

    // currentBlockPos for DDA traversal starts at the initial ray voxel position.
    glm::i64vec3 currentBlockPos = initialRayVoxelPos_i64;

    // Calculate the ray's origin relative to the corner of its starting voxel.
    // These components will be in the range [0, 1).
    // This avoids using a potentially large absoluteRayOrigin float for tMax calculation.
    glm::vec3 rayOriginInStartVoxel(
        originLocalPos.x - std::floor(originLocalPos.x),
        originLocalPos.y - std::floor(originLocalPos.y),
        originLocalPos.z - std::floor(originLocalPos.z)
    );
    
    // Ray step directions (1, 0, or -1)
    glm::ivec3 step;
    step.x = (rayDirection.x > 0) ? 1 : ((rayDirection.x < 0) ? -1 : 0);
    step.y = (rayDirection.y > 0) ? 1 : ((rayDirection.y < 0) ? -1 : 0);
    step.z = (rayDirection.z > 0) ? 1 : ((rayDirection.z < 0) ? -1 : 0);

    // Distance along the ray to the next voxel boundary
    glm::vec3 tMax;
    if (rayDirection.x > 0.0f) tMax.x = (1.0f - rayOriginInStartVoxel.x) / rayDirection.x;
    else if (rayDirection.x < 0.0f) tMax.x = -rayOriginInStartVoxel.x / rayDirection.x; // (0.0f - rayOriginInStartVoxel.x)
    else tMax.x = std::numeric_limits<float>::infinity();

    if (rayDirection.y > 0.0f) tMax.y = (1.0f - rayOriginInStartVoxel.y) / rayDirection.y;
    else if (rayDirection.y < 0.0f) tMax.y = -rayOriginInStartVoxel.y / rayDirection.y;
    else tMax.y = std::numeric_limits<float>::infinity();

    if (rayDirection.z > 0.0f) tMax.z = (1.0f - rayOriginInStartVoxel.z) / rayDirection.z;
    else if (rayDirection.z < 0.0f) tMax.z = -rayOriginInStartVoxel.z / rayDirection.z;
    else tMax.z = std::numeric_limits<float>::infinity();

    // Distance the ray has to travel to cross an entire voxel (delta t)
    glm::vec3 tDelta;
    tDelta.x = (rayDirection.x != 0.0f) ? std::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::infinity();
    tDelta.y = (rayDirection.y != 0.0f) ? std::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::infinity();
    tDelta.z = (rayDirection.z != 0.0f) ? std::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::infinity();

    float currentDistance = 0.0f;
    glm::ivec3 hitNormal(0);

    // Initial check: if starting inside a solid block, we might want to handle it.
    // For now, this algorithm finds the first block *entered*.
    // If rayOrigin is exactly on a boundary, behavior might need fine-tuning.

    while (currentDistance < MAX_RAY_DISTANCE) {
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                currentDistance = tMax.x;
                currentBlockPos.x += step.x;
                tMax.x += tDelta.x;
                hitNormal = glm::ivec3(-step.x, 0, 0);
            } else {
                currentDistance = tMax.z;
                currentBlockPos.z += step.z;
                tMax.z += tDelta.z;
                hitNormal = glm::ivec3(0, 0, -step.z);
            }
        } else {
            if (tMax.y < tMax.z) {
                currentDistance = tMax.y;
                currentBlockPos.y += step.y;
                tMax.y += tDelta.y;
                hitNormal = glm::ivec3(0, -step.y, 0);
            } else {
                currentDistance = tMax.z;
                currentBlockPos.z += step.z;
                tMax.z += tDelta.z;
                hitNormal = glm::ivec3(0, 0, -step.z);
            }
        }

        if (currentDistance >= MAX_RAY_DISTANCE) break; // Exceeded max distance

        // Check if currentBlockPos (i64vec3) is within the int32_t range
        // before querying the world or creating a RaycastResult.
        if (currentBlockPos.x >= std::numeric_limits<int>::min() && currentBlockPos.x <= std::numeric_limits<int>::max() &&
            currentBlockPos.y >= std::numeric_limits<int>::min() && currentBlockPos.y <= std::numeric_limits<int>::max() &&
            currentBlockPos.z >= std::numeric_limits<int>::min() && currentBlockPos.z <= std::numeric_limits<int>::max()) {
            
            glm::ivec3 queryableBlockPos(
                static_cast<int>(currentBlockPos.x),
                static_cast<int>(currentBlockPos.y),
                static_cast<int>(currentBlockPos.z)
            );

            uint16_t blockID = world.getBlockID(queryableBlockPos);
            if (blockID != Blocks::AIR_ID) { 
                const Block* blockDef = blockRegistry.getBlockDefinition(blockID);
                if (blockDef) {
                    const auto& optionalShape = blockDef->getCustomShape();
                    if (optionalShape.has_value() && !optionalShape.value().isEmpty()) {
                        const Physics::VoxelShape& shape = optionalShape.value();
                        // Calculate the ray's origin in the local space of the current block being tested.
                        // initialRayVoxelPos_i64: integer world coord of the ray's start voxel.
                        // currentBlockPos: integer world coord of the block being tested by DDA.
                        // rayOriginInStartVoxel: fractional part of ray's origin in its start voxel.
                        glm::vec3 rayOriginInBlockLocal = glm::vec3(initialRayVoxelPos_i64 - currentBlockPos) + rayOriginInStartVoxel;

                        Physics::VoxelShapeRayHit shapeHit = shape.intersect(
                            rayOriginInBlockLocal,
                            rayDirection
                        );

                        if (shapeHit.isHit && shapeHit.distance >= 0.0f && shapeHit.distance < MAX_RAY_DISTANCE) {
                            // Ensure the hit normal from VoxelShape is ivec3 if RaycastResult expects it
                            // VoxelShapeRayHit::normal is vec3, RaycastResult::hitNormal is ivec3
                            // We might need to adjust RaycastResult or convert here.
                            // For now, let's assume we can cast/round. A more robust solution might be needed.
                            glm::ivec3 finalHitNormal = glm::ivec3(glm::round(shapeHit.normal));
                            return RaycastResult(queryableBlockPos, finalHitNormal, shapeHit.distance);
                        }
                        // If custom shape was missed, or hit too far, continue ray marching
                    } else {
                        // No custom shape or empty shape, use DDA result (standard 1x1x1 cube)
                        return RaycastResult(queryableBlockPos, hitNormal, currentDistance);
                    }
                } // else blockDef is null, treat as air (should not happen if ID is not AIR)
            }
        } else {
            // Current block position is outside the addressable range of World::getBlockID
            // or RaycastResult. Treat as empty space and continue ray.
            // This ensures the ray can pass through "unaddressable" regions.
        }
    }

    return RaycastResult(); // No hit within MAX_RAY_DISTANCE
}

} // namespace VoxelRaycaster