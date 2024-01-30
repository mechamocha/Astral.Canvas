#include "Graphics/RenderProgram.hpp"
#include "Graphics/CurrentBackend.hpp"
#include "ErrorHandling.hpp"

#ifdef ASTRALCANVAS_VULKAN
#include "Graphics/Vulkan/VulkanInstanceData.hpp"
#endif

namespace AstralCanvas
{
    IAllocator dynamicProgramCacheAllocator = {};
    collections::hashmap<RenderProgramSignature, RenderProgram> dynamicProgramCache;

    RenderProgram::RenderProgram()
    {
        this->attachments = collections::vector<RenderProgramImageAttachment>();
        this->renderPasses = collections::vector<RenderPass>();
        this->allocator = NULL;
        this->handle = NULL;
    }
    RenderProgram::RenderProgram(IAllocator *allocator)
    {
        this->attachments = collections::vector<RenderProgramImageAttachment>(allocator);
        this->renderPasses = collections::vector<RenderPass>(allocator);
    }
    i32 RenderProgram::AddAttachment(ImageFormat imageFormat, bool clearColor, bool clearDepth, RenderPassOutputType outputType)
    {
        i32 result = (i32)this->attachments.count;

        if (imageFormat == ImageFormat_BackbufferFormat)
        {
            imageFormat = AstralCanvasVk_GetCurrentSwapchain()->imageFormat;
        }

        RenderProgramImageAttachment imageAttachment{
            imageFormat,
            clearColor,
            clearDepth,
            this,
            outputType};
        this->attachments.Add(imageAttachment);

        return result;
    }
    void RenderProgram::AddRenderPass(collections::Array<i32> colorAttachmentIDs, i32 depthAttachmentID)
    {
        this->renderPasses.Add(
            {
                colorAttachmentIDs,
                depthAttachmentID,
                this
            }
        );
    }
    void RenderProgram::AddRenderPass(IAllocator *allocator, i32 colorAttachmentID, i32 depthAttachmentID)
    {
        collections::Array<i32> colorAttachmentIDs = collections::Array<i32>(allocator, 1);
        colorAttachmentIDs.data[0] = colorAttachmentID;

        this->renderPasses.Add(
            {
                colorAttachmentIDs,
                depthAttachmentID,
                this
            }
        );
    }
    void RenderProgram::Construct()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                RenderProgram *program = this;
                if (program->renderPasses.count <= 0 || program->attachments.count <= 0)
                {
                    return;
                }

                IAllocator defaultAllocator = GetCAllocator();
                //use a single vector of VkAttachmentReferences with slices given to each VkSubpassDescription for simplicity
                collections::vector<VkAttachmentReference> subpassDescriptionAttachmentRefs = collections::vector<VkAttachmentReference>(&defaultAllocator);
                VkSubpassDescription *subpassDescriptions = (VkSubpassDescription*)malloc(sizeof(VkSubpassDescription) * program->renderPasses.count);
                
                for (usize i = 0; i < program->renderPasses.count; i++)
                {
                    RenderPass renderPassData = program->renderPasses.ptr[i];
                    subpassDescriptions[i] = {};
                    subpassDescriptions[i].flags = 0;

                    //input attachments are currently unused
                    subpassDescriptions[i].inputAttachmentCount = 0;
                    subpassDescriptions[i].pInputAttachments = NULL;

                    //references to the color attachments
                    //can have multiple color attachments (The shader can read from multiple input buffers)
                    u32 startCount = (u32)subpassDescriptionAttachmentRefs.count;
                    for (usize j = 0; j < renderPassData.colorAttachmentIndices.length; j++)
                    {
                        VkAttachmentReference ref;
                        ref.attachment = renderPassData.colorAttachmentIndices.data[j];
                        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        
                        subpassDescriptionAttachmentRefs.Add(ref);
                    }

                    subpassDescriptions[i].colorAttachmentCount = (u32)renderPassData.colorAttachmentIndices.length;
                    subpassDescriptions[i].pColorAttachments = &subpassDescriptionAttachmentRefs.ptr[startCount];

                    //lastly, depth attachment (if any)
                    if (renderPassData.depthAttachmentIndex > -1)
                    {
                        VkAttachmentReference ref;
                        ref.attachment = renderPassData.depthAttachmentIndex;
                        ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        subpassDescriptionAttachmentRefs.Add(ref);

                        subpassDescriptions[i].pDepthStencilAttachment = &subpassDescriptionAttachmentRefs.ptr[subpassDescriptionAttachmentRefs.count - 1];
                    }

                    //no clue what these are AKA dont need them rn
                    subpassDescriptions[i].preserveAttachmentCount = 0;
                    subpassDescriptions[i].pResolveAttachments = NULL;
                    subpassDescriptions[i].pResolveAttachments = NULL;

                    subpassDescriptions[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                }

                VkAttachmentDescription *attachmentDescriptions = (VkAttachmentDescription *)malloc(sizeof(VkAttachmentDescription) * program->attachments.count);
                for (usize i = 0; i < program->attachments.count; i++)
                {
                    RenderProgramImageAttachment attachmentData = program->attachments.ptr[i];
                    attachmentDescriptions[i] = {};
                    attachmentDescriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
                    attachmentDescriptions[i].flags = 0;
                    attachmentDescriptions[i].format = AstralCanvasVk_FromImageFormat(program->attachments.ptr[i].imageFormat);
                    if (attachmentData.imageFormat < ImageFormat_DepthNone)
                    {
                        attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        if (attachmentData.outputType == RenderPassOutput_ToNextPass)// || i < program->attachments.count - 1)
                        {
                            attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        }
                        else if (attachmentData.outputType == RenderPassOutput_ToRenderTarget)
                        {
                            attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        }
                        else
                        {
                            attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                        }

                        if (attachmentData.clearColor)
                        {
                            attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                        }
                        else
                        {
                            attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                        }
                    }
                    else
                    {
                        attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        if (attachmentData.clearDepth)
                        {
                            attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                            if (attachmentData.imageFormat == ImageFormat_Depth16Stencil8 || attachmentData.imageFormat == ImageFormat_Depth24Stencil8)
                            {
                                attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            }
                        }
                        else
                        {
                            attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                            attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                            if (attachmentData.imageFormat == ImageFormat_Depth16Stencil8 || attachmentData.imageFormat == ImageFormat_Depth24Stencil8)
                            {
                                attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                            }
                        }
                    }
                }

                //create dependencies in order that renderpasses were placed into the program.
                //Should (probably) make this customisable in the future
                //No non-linear/complex renderprograms for now
                VkSubpassDependency *subpassDependencies = (VkSubpassDependency*)malloc(sizeof(VkSubpassDependency) * program->renderPasses.count - 1);
                for (usize i = 0; i < program->renderPasses.count - 1; i++)
                {
                    subpassDependencies[i].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
                    subpassDependencies[i].srcSubpass = (u32)i;
                    subpassDependencies[i].dstSubpass = (u32)(i + 1);
                    subpassDependencies[i].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    subpassDependencies[i].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    subpassDependencies[i].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    subpassDependencies[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                }

                VkRenderPassCreateInfo programCreateInfo = {};
                programCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

                programCreateInfo.attachmentCount = (u32)program->attachments.count;
                programCreateInfo.pAttachments = attachmentDescriptions;

                programCreateInfo.subpassCount = (u32)program->renderPasses.count;
                programCreateInfo.pSubpasses = subpassDescriptions;

                programCreateInfo.dependencyCount = program->renderPasses.count - 1;
                programCreateInfo.pDependencies = subpassDependencies;

                programCreateInfo.flags = 0;
                programCreateInfo.pNext = NULL;

                vkCreateRenderPass(AstralCanvasVk_GetCurrentGPU()->logicalDevice, &programCreateInfo, NULL, (VkRenderPass*)&this->handle);

                free(subpassDependencies);
                free(attachmentDescriptions);
                free(subpassDescriptions);
                subpassDescriptionAttachmentRefs.deinit();

                break;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend: RenderProgram Construct");
                break;
        }
    }
    void RenderProgram::deinit()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                this->attachments.deinit();
                for (usize i = 0; i < this->renderPasses.count; i++)
                {
                    this->renderPasses.ptr[i].colorAttachmentIndices.deinit();
                }
                this->renderPasses.deinit();
                vkDestroyRenderPass(AstralCanvasVk_GetCurrentGPU()->logicalDevice, (VkRenderPass)this->handle, NULL);
                
                break;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend: RenderProgram deinit");
                break;
        }
    }
    

    RenderProgram *GetDynamicRenderProgram(IAllocator *allocator, ImageFormat imageFormat, ImageFormat depthFormat, bool mustClear, bool willDrawToWindow)
    {
        if (dynamicProgramCache.allocator == NULL)
        {
            dynamicProgramCacheAllocator = GetCAllocator();
            dynamicProgramCache = collections::hashmap<RenderProgramSignature, RenderProgram>(&dynamicProgramCacheAllocator, &RenderProgramSignatureHash, &RenderProgramSignatureEql);
        }

        RenderProgramSignature signature{imageFormat, depthFormat, mustClear, willDrawToWindow};

        RenderProgram *result = dynamicProgramCache.Get(signature);
        if (result != NULL)
        {
            return result;
        }

        RenderProgram newProgram = RenderProgram(allocator);

        i32 outputAttachment = newProgram.AddAttachment(imageFormat, mustClear, mustClear, willDrawToWindow ? RenderPassOutput_ToWindow : RenderPassOutput_ToRenderTarget);
        
        if (depthFormat > ImageFormat_DepthNone)
        {
            i32 depthAttachment = newProgram.AddAttachment(depthFormat, mustClear, mustClear, willDrawToWindow ? RenderPassOutput_ToWindow : RenderPassOutput_ToRenderTarget);
            newProgram.AddRenderPass(allocator, outputAttachment, depthAttachment);
        }
        else
        {
            newProgram.AddRenderPass(allocator, outputAttachment, -1);
        }

        newProgram.Construct();

        return dynamicProgramCache.Add(signature, newProgram);
    }
}