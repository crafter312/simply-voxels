#include "VulkanBufferManager.hpp"
#include <stdexcept>
#include <cstring> // For memcpy
#include <algorithm> // For std::sort and std::max
#include <iostream> // For std::cerr (debugging)

static inline VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

VulkanBufferManager::VulkanBufferManager(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue)
    : deviceRef(device), physicalDeviceRef(physicalDevice), commandPoolRef(commandPool), graphicsQueueRef(graphicsQueue) {
    if (deviceRef == VK_NULL_HANDLE || physicalDeviceRef == VK_NULL_HANDLE || commandPoolRef == VK_NULL_HANDLE || graphicsQueueRef == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanBufferManager received null handles during construction!");
    }

    // --- Initialize Buffer Pools ---
    // Define a large size for our pooled buffers, e.g., 256MB
    const VkDeviceSize poolSize = 256 * 1024 * 1024;
    createNewManagedBuffer(poolSize, m_vertexBufferPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    createNewManagedBuffer(poolSize, m_indexBufferPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // --- Initialize reusable staging buffer ---
    m_stagingBufferSize = 16 * 1024 * 1024; // 16MB initial size
    createBuffer(m_stagingBufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_stagingBuffer,
                 m_stagingBufferMemory);

    // Create the fence for transfer operations, initially in a signaled state
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start as signaled so the first transfer doesn't wait
    if (vkCreateFence(deviceRef, &fenceInfo, nullptr, &m_transferFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transfer fence!");
    }

    vkMapMemory(deviceRef, m_stagingBufferMemory, 0, m_stagingBufferSize, 0, &m_stagingBufferMapped);
}

VulkanBufferManager::~VulkanBufferManager() {
    if (m_stagingBufferMapped) {
        vkUnmapMemory(deviceRef, m_stagingBufferMemory);
    }
    vkDestroyBuffer(deviceRef, m_stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, m_stagingBufferMemory, nullptr);

    for (auto& managedBuffer : m_vertexBufferPool) {
        vkDestroyBuffer(deviceRef, managedBuffer.buffer, nullptr);
        vkFreeMemory(deviceRef, managedBuffer.memory, nullptr);
    }
    for (auto& managedBuffer : m_indexBufferPool) {
        vkDestroyBuffer(deviceRef, managedBuffer.buffer, nullptr);
        vkFreeMemory(deviceRef, managedBuffer.memory, nullptr);
    }
    if (m_transferFence != VK_NULL_HANDLE) {
        vkDestroyFence(deviceRef, m_transferFence, nullptr);
        m_transferFence = VK_NULL_HANDLE;
    }
}

void VulkanBufferManager::createNewManagedBuffer(VkDeviceSize size, std::vector<ManagedBuffer>& pool, VkBufferUsageFlags usage) {
    ManagedBuffer newManagedBuffer;
    newManagedBuffer.totalSize = size;

    // Ensure all buffers in a pool have the same usage flags.
    // If the pool isn't empty, use the usage from the first buffer. Otherwise, use the provided usage.
    if (!pool.empty()) {
        newManagedBuffer.usage = pool.front().usage;
    } else {
        // All our mesh buffers need to be transfer destinations.
        newManagedBuffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
    }

    createBuffer(newManagedBuffer.totalSize,
                 newManagedBuffer.usage,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 newManagedBuffer.buffer,
                 newManagedBuffer.memory);

    // Initialize allocator bookkeeping to safe defaults
    newManagedBuffer.currentOffset = 0;
    newManagedBuffer.freeList.clear();            // no free-list fragments yet (bump alloc used initially)
    newManagedBuffer.activeAllocations.clear();   // no active allocations

    pool.push_back(newManagedBuffer);
}

void VulkanBufferManager::freeBufferRegion(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, std::vector<ManagedBuffer>& pool) {
    for (auto& managedBuffer : pool) {
        if (managedBuffer.buffer == buffer) {
            // Found the right ManagedBuffer. Add a new free block.
            auto& freeList = managedBuffer.freeList;
            freeList.push_back({offset, size});

            // Sort by offset to make merging easier.
            std::sort(freeList.begin(), freeList.end(), [](const FreeBlock& a, const FreeBlock& b) {
                return a.offset < b.offset;
            });

            // --- Corrected Merge Logic ---
            // The previous in-place erase loop was buggy. A safer approach is to build a new merged list.
            if (freeList.size() > 1) {
                std::vector<FreeBlock> mergedList;
                mergedList.push_back(freeList[0]);

                for (size_t i = 1; i < freeList.size(); ++i) {
                    FreeBlock& last = mergedList.back();
                    const FreeBlock& current = freeList[i];

                    if (last.offset + last.size >= current.offset) {
                        // Merge current into last
                        VkDeviceSize newEnd = std::max(last.offset + last.size, current.offset + current.size);
                        last.size = newEnd - last.offset;
                    } else {
                        // No overlap, just add the new block
                        mergedList.push_back(current);
                    }
                }
                freeList = std::move(mergedList); // Replace old list with the new merged one
            }
            // Debug: remove the allocation record matching this freed region
            for (auto itAlloc = managedBuffer.activeAllocations.begin(); itAlloc != managedBuffer.activeAllocations.end(); ++itAlloc) {
                if (itAlloc->offset == offset && itAlloc->size == size) {
                    managedBuffer.activeAllocations.erase(itAlloc);
                    break;
                }
            }
            return; // Done
        }
    }
    // If we get here, the buffer handle was not found in this specific pool.
    // This is not an error, as we might be trying to free a vertex buffer from the index pool, or vice-versa.
    // Simply return.
}

void VulkanBufferManager::freeVertexBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
    freeBufferRegion(buffer, offset, size, m_vertexBufferPool);
}

void VulkanBufferManager::freeIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
    freeBufferRegion(buffer, offset, size, m_indexBufferPool);
}

VkBuffer VulkanBufferManager::allocateBufferRegion(VkDeviceSize size, VkDeviceSize& outOffset, std::vector<ManagedBuffer>& pool, VkBufferUsageFlags usage, VkDeviceSize elementSize) {
    // Vulkan has alignment requirements for buffer offsets. 256 is a common and safe alignment.
    const VkDeviceSize baseAlignment = 256;

    for (auto& managedBuffer : pool) {
        // --- 1. Check the free list for a suitable block (First-Fit) ---
        for (auto it = managedBuffer.freeList.begin(); it != managedBuffer.freeList.end(); ++it) {
            FreeBlock block = *it;

            // Align the start of the free block's offset to base alignment
            VkDeviceSize alignedOffset = alignUp(block.offset, baseAlignment);

            // Enforce the element-specific alignment for vertex/index usages
            if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
                alignedOffset = alignUp(alignedOffset, elementSize); // onus is on caller to provide correct element size
            }
            else if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
                VkDeviceSize eAlign = static_cast<VkDeviceSize>(sizeof(uint32_t));
                alignedOffset = alignUp(alignedOffset, eAlign);
            }

            VkDeviceSize alignmentPadding = alignedOffset - block.offset;

            if (block.size >= size + alignmentPadding) {
                // This block is large enough.
                outOffset = alignedOffset;

                VkDeviceSize tailOffset = alignedOffset + size;
                VkDeviceSize tailSize = (block.offset + block.size) - tailOffset;

                // Remove the old free block and insert any leading/tailing fragments to
                // keep the free list in a sensible order. Using erase/insert keeps the
                // list deterministic and avoids surprises from swapping with back().
                size_t index = static_cast<size_t>(std::distance(managedBuffer.freeList.begin(), it));
                managedBuffer.freeList.erase(managedBuffer.freeList.begin() + index);

                if (alignmentPadding > 0) {
                    // Insert leading fragment at the original position
                    managedBuffer.freeList.insert(managedBuffer.freeList.begin() + index, {block.offset, alignmentPadding});
                    ++index; // Keep index pointing after the inserted leading fragment for tail insert
                }

                if (tailSize > 16) // Use a threshold to avoid tiny fragments
                    managedBuffer.freeList.insert(managedBuffer.freeList.begin() + index, {tailOffset, tailSize});

                // Debug: ensure we do not overlap an existing active allocation
                for (const auto& a : managedBuffer.activeAllocations) {
                    VkDeviceSize aStart = a.offset;
                    VkDeviceSize aEnd = a.offset + a.size;
                    VkDeviceSize newStart = outOffset;
                    VkDeviceSize newEnd = outOffset + size;
                    if (!(newEnd <= aStart || newStart >= aEnd)) {
                        std::cerr << "Allocation overlap detected in free-list path: new[" << newStart << "," << newEnd << "] overlaps existing[" << aStart << "," << aEnd << "]\n";
                        throw std::runtime_error("Detected overlapping allocation (debug)");
                    }
                }
                managedBuffer.activeAllocations.push_back({outOffset, size});
                return managedBuffer.buffer;
            }
        }

        // --- 2. If no free block found, use the bump allocator ---
        VkDeviceSize alignedBumpOffset = alignUp(managedBuffer.currentOffset, baseAlignment);

        // Enforce element-specific alignment for bump allocation too
        if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
            VkDeviceSize eAlign = static_cast<VkDeviceSize>(sizeof(Vertex));
            alignedBumpOffset = alignUp(alignedBumpOffset, eAlign);
        }
        if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
            VkDeviceSize eAlign = static_cast<VkDeviceSize>(sizeof(uint32_t));
            alignedBumpOffset = alignUp(alignedBumpOffset, eAlign);
        }

        if (alignedBumpOffset + size <= managedBuffer.totalSize) {
            // We found space at the end of the buffer
            outOffset = alignedBumpOffset;
            managedBuffer.currentOffset = alignedBumpOffset + size;
            // Debug: ensure we do not overlap an existing active allocation
            for (const auto& a : managedBuffer.activeAllocations) {
                VkDeviceSize aStart = a.offset;
                VkDeviceSize aEnd = a.offset + a.size;
                VkDeviceSize newStart = outOffset;
                VkDeviceSize newEnd = outOffset + size;
                if (!(newEnd <= aStart || newStart >= aEnd)) {
                    std::cerr << "Allocation overlap detected in bump path: new[" << newStart << "," << newEnd << "] overlaps existing[" << aStart << "," << aEnd << "]\n";
                    throw std::runtime_error("Detected overlapping allocation (debug)");
                }
            }
            managedBuffer.activeAllocations.push_back({outOffset, size});
            return managedBuffer.buffer;
        }
    }

    // --- 3. If no space anywhere in existing buffers, create a new ManagedBuffer ---
    const VkDeviceSize defaultPoolSize = 256 * 1024 * 1024;
    VkDeviceSize newPoolSize = std::max(defaultPoolSize, size);
    createNewManagedBuffer(newPoolSize, pool, usage);

    // Allocate from the beginning of the new buffer
    ManagedBuffer& newBuffer = pool.back(); // Get a reference to the new buffer
    outOffset = 0;
    // Align the bump offset for the new buffer
    VkDeviceSize alignedSize = alignUp(size, baseAlignment);
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        VkDeviceSize eAlign = static_cast<VkDeviceSize>(sizeof(Vertex));
        alignedSize = alignUp(alignedSize, eAlign);
    }
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        VkDeviceSize eAlign = static_cast<VkDeviceSize>(sizeof(uint32_t));
        alignedSize = alignUp(alignedSize, eAlign);
    }

    // mark consumed region and update freeList for the remainder
    newBuffer.currentOffset = alignedSize;

    // Update freeList to reflect that the [0, alignedSize) region is now used.
    newBuffer.freeList.clear();
    if (alignedSize < newBuffer.totalSize) {
        newBuffer.freeList.push_back({ alignedSize, newBuffer.totalSize - alignedSize });
    }

    // Debug: ensure no overlap in the new buffer
    for (const auto& a : newBuffer.activeAllocations) {
        VkDeviceSize aStart = a.offset;
        VkDeviceSize aEnd = a.offset + a.size;
        VkDeviceSize newStart = outOffset;
        VkDeviceSize newEnd = outOffset + size;
        if (!(newEnd <= aStart || newStart >= aEnd)) {
            std::cerr << "Allocation overlap detected in new-buffer path: new[" << newStart << "," << newEnd << "] overlaps existing[" << aStart << "," << aEnd << "]\n";
            throw std::runtime_error("Detected overlapping allocation (debug)");
        }
    }
    newBuffer.activeAllocations.push_back({outOffset, size});

    // It's important to return the buffer from the back of the vector, which is the one we just added.
    return newBuffer.buffer;
}

void VulkanBufferManager::resizeStagingBuffer(VkDeviceSize newSize) {
    // Wait for any pending copies using the graphics queue to finish.
    // Using vkQueueWaitIdle is safer than vkDeviceWaitIdle, especially during shutdown.
    vkQueueWaitIdle(graphicsQueueRef);

    // Cleanup old buffer
    vkUnmapMemory(deviceRef, m_stagingBufferMemory);
    vkDestroyBuffer(deviceRef, m_stagingBuffer, nullptr);
    vkFreeMemory(deviceRef, m_stagingBufferMemory, nullptr);

    // Create new, larger buffer
    m_stagingBufferSize = newSize;
    createBuffer(m_stagingBufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_stagingBuffer,
                 m_stagingBufferMemory);
    vkMapMemory(deviceRef, m_stagingBufferMemory, 0, m_stagingBufferSize, 0, &m_stagingBufferMapped);
}

uint32_t VulkanBufferManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDeviceRef, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanBufferManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(deviceRef, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(deviceRef, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(deviceRef, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    vkBindBufferMemory(deviceRef, buffer, bufferMemory, 0);
}

VkCommandBuffer VulkanBufferManager::beginTransferCommands() {
    // Wait for the *previous* transfer to finish before we start a new one.
    // This ensures we don't overwrite the staging buffer while it's still being read by the GPU.
    // The timeout is set to a huge value to effectively wait indefinitely.
    vkWaitForFences(deviceRef, 1, &m_transferFence, VK_TRUE, UINT64_MAX);
    vkResetFences(deviceRef, 1, &m_transferFence);
    
    // Reset the staging buffer offset for a new batch of transfers.
    m_stagingBufferCurrentOffset = 0;

    VkCommandBufferAllocateInfo allocInfo{};
    // This is now just a helper to allocate and begin a command buffer.
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPoolRef;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(deviceRef, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanBufferManager::endAndSubmitTransferCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Submit the command buffer and signal m_transferFence when it's done.
    // The next call to beginTransferCommands will wait on this fence.
    vkQueueSubmit(graphicsQueueRef, 1, &submitInfo, m_transferFence);

    // CRITICAL FIX: We must wait for this transfer to complete before freeing the command buffer.
    // Not waiting here caused a deadlock, as the command buffer could be freed while in use.
    // This also ensures that by the time this function returns, the data is on the GPU.
    vkWaitForFences(deviceRef, 1, &m_transferFence, VK_TRUE, UINT64_MAX);

    vkFreeCommandBuffers(deviceRef, commandPoolRef, 1, &commandBuffer);
}

void VulkanBufferManager::waitForTransfersToFinish() {
    // This function can be called during cleanup or before operations that
    // need to ensure all pending transfers are complete.
    if (m_transferFence != VK_NULL_HANDLE) {
        vkWaitForFences(deviceRef, 1, &m_transferFence, VK_TRUE, UINT64_MAX);
    }
}


void VulkanBufferManager::copyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
}

VkBuffer VulkanBufferManager::createVertexBuffer(VkCommandBuffer& commandBuffer, const std::vector<Vertex>& vertices, VkDeviceSize& outVertexOffset) {
    if (vertices.empty()) {
        outVertexOffset = 0;
        return VK_NULL_HANDLE;
    }
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // Get the current offset in the staging buffer before this copy
    VkDeviceSize stagingOffset = m_stagingBufferCurrentOffset;

    // Safely resize staging buffer if needed
    if (stagingOffset + bufferSize > m_stagingBufferSize) {
        // 1. Flush and submit the current command buffer to finish all pending copies.
        endAndSubmitTransferCommands(commandBuffer);
        // 2. Now it's safe to resize the staging buffer.
        resizeStagingBuffer(std::max(m_stagingBufferSize * 2, stagingOffset + bufferSize));
        // 3. Begin a new command buffer for subsequent transfers and update the caller's handle.
        commandBuffer = beginTransferCommands();
        stagingOffset = m_stagingBufferCurrentOffset; // The offset is now 0.
    }

    // Get a region in the device-local vertex buffer pool
    VkBuffer poolBuffer = allocateBufferRegion(bufferSize, outVertexOffset, m_vertexBufferPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, static_cast<VkDeviceSize>(sizeof(Vertex)));

    // Copy data to staging buffer
    memcpy(static_cast<char*>(m_stagingBufferMapped) + stagingOffset, vertices.data(), (size_t)bufferSize);
    m_stagingBufferCurrentOffset += bufferSize; // Bump the offset

    // Copy from staging buffer to the allocated region in the device-local buffer
    copyBuffer(commandBuffer, m_stagingBuffer, poolBuffer, stagingOffset, outVertexOffset, bufferSize);
    return poolBuffer;
}

VkBuffer VulkanBufferManager::createVertexBuffer(
    VkCommandBuffer& commandBuffer,
    const std::vector<WireframeMesher::WireframeVertex>& vertices,
    VkDeviceSize& outVertexOffset) {

    if (vertices.empty()) {
        outVertexOffset = 0;
        return VK_NULL_HANDLE;
    }

    VkDeviceSize bufferSize = sizeof(WireframeMesher::WireframeVertex) * vertices.size();

    // Get the current offset in the staging buffer before this copy
    VkDeviceSize stagingOffset = m_stagingBufferCurrentOffset;

    // Safely resize staging buffer if needed
    if (stagingOffset + bufferSize > m_stagingBufferSize) {
        // 1. Flush and submit the current command buffer.
        endAndSubmitTransferCommands(commandBuffer);
        // 2. Resize the staging buffer.
        resizeStagingBuffer(std::max(m_stagingBufferSize * 2, stagingOffset + bufferSize));
        // 3. Begin a new command buffer and update the caller's handle.
        commandBuffer = beginTransferCommands();
        stagingOffset = m_stagingBufferCurrentOffset; // The offset is now 0.
    }

    // Get a region in the device-local vertex buffer pool
    VkBuffer poolBuffer = allocateBufferRegion(bufferSize, outVertexOffset, m_vertexBufferPool, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, static_cast<VkDeviceSize>(sizeof(WireframeMesher::WireframeVertex)));

    // Copy data to staging buffer
    memcpy(static_cast<char*>(m_stagingBufferMapped) + stagingOffset, vertices.data(), (size_t)bufferSize);
    m_stagingBufferCurrentOffset += bufferSize; // Bump the offset

    // Copy from staging buffer to the allocated region in the device-local buffer
    copyBuffer(commandBuffer, m_stagingBuffer, poolBuffer, stagingOffset, outVertexOffset, bufferSize);
    return poolBuffer;
}

VkBuffer VulkanBufferManager::createIndexBuffer(VkCommandBuffer& commandBuffer, const std::vector<uint32_t>& indices, VkDeviceSize& outIndexOffset) {
    if (indices.empty()) {
        outIndexOffset = 0;
        return VK_NULL_HANDLE;
    }
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    // Get the current offset in the staging buffer before this copy
    VkDeviceSize stagingOffset = m_stagingBufferCurrentOffset;

    // Safely resize staging buffer if needed
    if (stagingOffset + bufferSize > m_stagingBufferSize) {
        // 1. Flush and submit the current command buffer.
        endAndSubmitTransferCommands(commandBuffer);
        // 2. Resize the staging buffer.
        resizeStagingBuffer(std::max(m_stagingBufferSize * 2, stagingOffset + bufferSize));
        // 3. Begin a new command buffer and update the caller's handle.
        commandBuffer = beginTransferCommands();
        stagingOffset = m_stagingBufferCurrentOffset; // The offset is now 0.
    }

    // Get a region in the device-local index buffer pool
    VkBuffer poolBuffer = allocateBufferRegion(bufferSize, outIndexOffset, m_indexBufferPool, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, static_cast<VkDeviceSize>(sizeof(uint32_t)));

    // Copy data to staging buffer
    memcpy(static_cast<char*>(m_stagingBufferMapped) + stagingOffset, indices.data(), (size_t)bufferSize);
    m_stagingBufferCurrentOffset += bufferSize; // Bump the offset

    // Copy from staging buffer to the allocated region in the device-local buffer
    copyBuffer(commandBuffer, m_stagingBuffer, poolBuffer, stagingOffset, outIndexOffset, bufferSize);
    return poolBuffer;
}

void VulkanBufferManager::createUniformBuffers(uint32_t numBuffers, VkDeviceSize bufferSize, std::vector<VkBuffer>& outUniformBuffers, std::vector<VkDeviceMemory>& outUniformBuffersMemory, std::vector<void*>& outUniformBuffersMapped) {
    outUniformBuffers.resize(numBuffers);
    outUniformBuffersMemory.resize(numBuffers);
    outUniformBuffersMapped.resize(numBuffers);

    for (uint32_t i = 0; i < numBuffers; ++i) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, outUniformBuffers[i], outUniformBuffersMemory[i]);
        vkMapMemory(deviceRef, outUniformBuffersMemory[i], 0, bufferSize, 0, &outUniformBuffersMapped[i]);
    }
}

VkFormat VulkanBufferManager::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDeviceRef, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported format!");
}

VkFormat VulkanBufferManager::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool VulkanBufferManager::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void VulkanBufferManager::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                     VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Or VK_IMAGE_LAYOUT_PREINITIALIZED if data is uploaded
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // No MSAA for the depth image itself for now
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(deviceRef, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(deviceRef, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(deviceRef, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(deviceRef, image, imageMemory, 0);
}

VkImageView VulkanBufferManager::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(deviceRef, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view for depth/texture!");
    }
    return imageView;
}

void VulkanBufferManager::createDepthResources(VkExtent2D swapChainExtent, VkImage& outDepthImage, VkDeviceMemory& outDepthImageMemory, VkImageView& outDepthImageView, VkFormat& outDepthFormat) {
    outDepthFormat = findDepthFormat();
    createImage(swapChainExtent.width, swapChainExtent.height, outDepthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outDepthImage, outDepthImageMemory);
    outDepthImageView = createImageView(outDepthImage, outDepthFormat, hasStencilComponent(outDepthFormat) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT);
}