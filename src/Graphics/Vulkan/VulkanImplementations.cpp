#include "Graphics/Vulkan/VulkanImplementations.hpp"
#include "Graphics/Vulkan/VulkanEnumConverters.hpp"
#include "Graphics/Vulkan/VulkanGPU.hpp"

VkBuffer AstralCanvasVk_CreateResourceBuffer(AstralVulkanGPU *gpu, usize size, VkBufferUsageFlags usageFlags)
{
    VkBufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.size = size;
    createInfo.usage = usageFlags;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer result;
    if (vkCreateBuffer(gpu->logicalDevice, &createInfo, NULL, &result) == VK_SUCCESS)
    {
        return result;
    }
    return NULL;
}
VkCommandBuffer AstralCanvasVk_CreateTransientCommandBuffer(AstralVulkanGPU *gpu, AstralCanvasVkCommandQueue *queueToUse, bool alsoBeginBuffer)
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    allocInfo.commandPool = queueToUse->transientCommandPool;
    allocInfo.pNext = NULL;

    queueToUse->commandPoolMutex.EnterLock();

    VkCommandBuffer result;
    if (vkAllocateCommandBuffers(gpu->logicalDevice, &allocInfo, &result) != VK_SUCCESS)
    {
        result = NULL;
    }

    queueToUse->commandPoolMutex.ExitLock();

    if (alsoBeginBuffer && result != NULL)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pInheritanceInfo = NULL;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(result, &beginInfo);
    }
    return result;
}
void AstralCanvasVk_EndTransientCommandBuffer(AstralVulkanGPU *gpu, AstralCanvasVkCommandQueue *queueToUse, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    //if we are on the graphics queue, the queue may also be used for presenting
    //hence, we need to wait for the queue to finish presenting before we can transition the image
    if (gpu->DedicatedGraphicsQueue.queue == queueToUse->queue)
    {
        queueToUse->queueMutex.EnterLock();

        //alternatively, wait for queue idle
        vkWaitForFences(gpu->logicalDevice, 1, &queueToUse->queueFence, true, UINT64_MAX);

        queueToUse->queueMutex.ExitLock();
    }

    //submit the queue
    queueToUse->queueMutex.EnterLock();

    if (vkQueueSubmit(queueToUse->queue, 1, &submitInfo, NULL) != VK_SUCCESS)
    {
        queueToUse->queueMutex.ExitLock();
        THROW_ERR("Failed to submit queue");
    }
    //todo: see if can transfer this to a fence or something
    vkQueueWaitIdle(queueToUse->queue);

    queueToUse->queueMutex.ExitLock();

    //finally, free the command buffer
    queueToUse->commandPoolMutex.EnterLock();

    vkFreeCommandBuffers(gpu->logicalDevice, queueToUse->transientCommandPool, 1, &commandBuffer);

    queueToUse->commandPoolMutex.ExitLock();
}

void AstralCanvasVk_TransitionTextureLayout(AstralVulkanGPU *gpu, Texture2D *texture, VkImageAspectFlags aspectFlags, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier memBarrier = {};
    memBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    memBarrier.oldLayout = oldLayout;
    memBarrier.newLayout = newLayout;
    memBarrier.srcQueueFamilyIndex = 0;
    memBarrier.dstQueueFamilyIndex = 0;
    memBarrier.image = (VkImage)texture->imageHandle;
    memBarrier.subresourceRange.aspectMask = aspectFlags;
    memBarrier.subresourceRange.baseMipLevel = 0;
    memBarrier.subresourceRange.levelCount = texture->mipLevels;
    memBarrier.subresourceRange.baseArrayLayer = 0;
    memBarrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    AstralCanvasVkCommandQueue *cmdQueue = &gpu->DedicatedGraphicsQueue;
    VkCommandBuffer cmdBuffer = AstralCanvasVk_CreateTransientCommandBuffer(gpu, cmdQueue, true);

    if (cmdBuffer != NULL)
    {
        VkPipelineStageFlags depthStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        VkPipelineStageFlags sampledStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        switch (oldLayout)
        {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                break;

            case VK_IMAGE_LAYOUT_GENERAL:
                sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                sourceStage = depthStageMask;
                memBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                sourceStage = depthStageMask | sampledStageMask;
                break;

            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                sourceStage = sampledStageMask;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                sourceStage = VK_PIPELINE_STAGE_HOST_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;

            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                break;

            default:
                THROW_ERR("Invalid source layout!");
                break;
        }

        switch (newLayout)
        {
            switch (newLayout)
            {
                case VK_IMAGE_LAYOUT_GENERAL:
                    destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                    break;

                case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                    destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    break;

                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                    destinationStage = depthStageMask;
                    memBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;

                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                    destinationStage = depthStageMask | sampledStageMask;
                    memBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
                    break;

                case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
                    break;

                case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    break;

                case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    memBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    break;

                case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                    // vkQueuePresentKHR performs automatic visibility operations
                    break;

                default:
                    THROW_ERR("Invalid destination layout!");
            }
        }

        cmdQueue->commandPoolMutex.EnterLock();
        vkCmdPipelineBarrier(cmdBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &memBarrier);
        cmdQueue->commandPoolMutex.ExitLock();

        AstralCanvasVk_EndTransientCommandBuffer(gpu, cmdQueue, cmdBuffer);
    }
}
void AstralCanvasVk_CopyBufferToImage(AstralVulkanGPU *gpu, VkBuffer from, Texture2D *to)
{
    VkCommandBuffer transientCmdBuffer = AstralCanvasVk_CreateTransientCommandBuffer(gpu, &gpu->DedicatedGraphicsQueue, true);

    VkBufferImageCopy bufferImageCopy = {};
    
    bufferImageCopy.bufferOffset = 0;
    //only set values other than 0 if the image buffer is not tightly packed
    bufferImageCopy.bufferRowLength = 0;
    bufferImageCopy.bufferImageHeight = 0;

    bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferImageCopy.imageSubresource.mipLevel = 0;
    bufferImageCopy.imageSubresource.baseArrayLayer = 0;
    bufferImageCopy.imageSubresource.layerCount = 1;

    bufferImageCopy.imageOffset = {};
    bufferImageCopy.imageExtent.width = to->width;
    bufferImageCopy.imageExtent.height = to->height;
    bufferImageCopy.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(
        transientCmdBuffer,
        from,
        (VkImage)to->imageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, //length of things to copy
        &bufferImageCopy
        );

    AstralCanvasVk_EndTransientCommandBuffer(gpu, &gpu->DedicatedGraphicsQueue, transientCmdBuffer);
}

void AstralCanvasVk_CreateSamplerState(AstralVulkanGPU *gpu, SamplerState *samplerState)
{
    VkSamplerCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = AstralCanvasVk_FromSampleMode(samplerState->sampleMode);
    createInfo.minFilter = AstralCanvasVk_FromSampleMode(samplerState->sampleMode);

    VkSamplerAddressMode mode = AstralCanvasVk_FromRepeatMode(samplerState->repeatMode);

    createInfo.addressModeU = mode;
    createInfo.addressModeV = mode;
    createInfo.addressModeW = mode;

    createInfo.anisotropyEnable = (i32)samplerState->anisotropic;
    createInfo.maxAnisotropy = samplerState->anisotropyLevel;
    createInfo.unnormalizedCoordinates = (i32)false;
    createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    createInfo.compareEnable = false;
    createInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo.mipLodBias = 0.0f;
    createInfo.minLod = 0.0f;
    createInfo.maxLod = 0.0f;

    VkSampler sampler;
    if (vkCreateSampler(gpu->logicalDevice, &createInfo, NULL, &sampler) != VK_SUCCESS)
    {
        THROW_ERR("Failed to create sampler");
        samplerState->constructed = true;
    }
}
void AstralCanvasVk_DestroySamplerState(AstralVulkanGPU *gpu, SamplerState *samplerState)
{
    vkDestroySampler(gpu->logicalDevice, (VkSampler)samplerState->handle, NULL);
}

void AstralCanvasVk_DestroyTexture2D(AstralVulkanGPU *gpu, Texture2D *texture)
{
    if (!texture->isDisposed && texture->constructed)
    {
        vkDestroyImageView(gpu->logicalDevice, (VkImageView)texture->imageView, NULL);
        vkDestroyImage(gpu->logicalDevice, (VkImage)texture->imageHandle, NULL);
        vmaFreeMemory(AstralCanvasVk_GetCurrentVulkanAllocator(), texture->allocatedMemory.vkAllocation);
        texture->isDisposed = true;
    }
}
void AstralCanvasVk_CreateTexture2D(AstralVulkanGPU *gpu, Texture2D *texture)
{
    VkImage image;
    if (texture->imageHandle == NULL)
    {
        VkImageCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.extent.width = texture->width;
        createInfo.extent.height = texture->height;
        createInfo.extent.depth = 1;

        createInfo.mipLevels = texture->mipLevels;
        createInfo.arrayLayers = 1;
        createInfo.format = AstralCanvasVk_FromImageFormat(texture->imageFormat);
        createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (createInfo.format >= ImageFormat_DepthNone)
        {
            createInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        else
        {
            if (texture->usedForRenderTarget)
            {
                createInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            else
            {
                createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            }
        }
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        createInfo.flags = 0;

        if (vkCreateImage(gpu->logicalDevice, &createInfo, NULL, (VkImage*)&texture->imageHandle) != VK_SUCCESS)
        {
            THROW_ERR("Failed to create image");
        }
    }
    image = (VkImage)texture->imageHandle;

    if (texture->bytes != NULL && (texture->width * texture->height > 0) && texture->ownsHandle)
    {
        //VmaAllocation
        texture->allocatedMemory = AstralCanvasVk_AllocateMemoryForImage(image, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (!texture->usedForRenderTarget)
        {
            VkBuffer stagingBuffer = AstralCanvasVk_CreateResourceBuffer(gpu, texture->width * texture->height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

            AstralCanvasMemoryAllocation stagingMemory = AstralCanvasVk_AllocateMemoryForBuffer(stagingBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU, (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), true);

            memcpy(stagingMemory.vkAllocationInfo.pMappedData, texture->bytes, (usize)(texture->width * texture->height * 4));

            AstralCanvasVk_TransitionTextureLayout(gpu, texture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            AstralCanvasVk_CopyBufferToImage(gpu, stagingBuffer, texture);

            AstralCanvasVk_TransitionTextureLayout(gpu, texture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(gpu->logicalDevice, stagingBuffer, NULL);

            vmaFreeMemory(AstralCanvasVk_GetCurrentVulkanAllocator(), stagingMemory.vkAllocation);
        }
    }
    else
    {
        if (texture->imageFormat >= ImageFormat_DepthNone)
        {
            AstralCanvasVk_TransitionTextureLayout(gpu, texture, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        }
        else AstralCanvasVk_TransitionTextureLayout(gpu, texture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    VkImageViewCreateInfo viewCreateInfo = {};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.image = image;
    viewCreateInfo.format = AstralCanvasVk_FromImageFormat(texture->imageFormat);
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    if (texture->imageFormat >= ImageFormat_DepthNone)
    {
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else 
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.levelCount = texture->mipLevels;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;

    viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    if (vkCreateImageView(gpu->logicalDevice, &viewCreateInfo, NULL, (VkImageView*)&texture->imageView) != VK_SUCCESS)
    {
        THROW_ERR("Failed to create image view");
    }

    texture->constructed = true;
}

void AstralCanvasVk_CreateRenderTarget(AstralVulkanGPU *gpu, RenderTarget *renderTarget)
{
    VkFramebufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.width = renderTarget->width;
    createInfo.height = renderTarget->height;
    createInfo.layers = 1;

    VkImageView imageViews[2];
    imageViews[0] = (VkImageView)renderTarget->backendTexture.imageView;

    createInfo.attachmentCount = 1;
    if (renderTarget->depthBuffer.imageHandle != NULL)
    {
        createInfo.attachmentCount += 1;
        imageViews[1] = (VkImageView)renderTarget->depthBuffer.imageView;
    }

    //createInfo requires a renderpass to set what renderpass the framebuffer is
    //compatible with. Even if we pass in a different renderpass that is compatible

    //with the one it is created with, it will still be alright
    //https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#renderpass-compatibility

    if (renderTarget->isBackbuffer)
    {

    }
    else
    {
        
    }
}