#include "Graphics/RenderPipeline.hpp"
#include "Graphics/CurrentBackend.hpp"
#include "ErrorHandling.hpp"
#include "ArenaAllocator.hpp"

#ifdef ASTRALCANVAS_VULKAN
#include "Graphics/Vulkan/VulkanInstanceData.hpp"
#include "Graphics/Vulkan/VulkanEnumConverters.hpp"
#include "Graphics/Vulkan/VulkanVertex.hpp"
#endif

namespace AstralCanvas
{
    RenderPipeline::RenderPipeline(IAllocator *allocator, Shader *pipelineShader, CullMode pipelineCullMode, PrimitiveType pipelinePrimitiveType, BlendState *pipelineBlendState, bool testDepth, bool writeToDepth, collections::Array<VertexDeclaration> pipelineVertexDeclarations)
    {
        this->shader = pipelineShader;
        this->cullMode = pipelineCullMode;
        this->primitiveType = pipelinePrimitiveType;
        this->blendState = pipelineBlendState;
        this->depthTest = testDepth;
        this->depthWrite = writeToDepth;
        this->vertexDeclarations = pipelineVertexDeclarations;
        this->zoneToPipelineInstance = collections::hashmap<RenderPipelineBindZone, void *>(allocator, &RenderPipelineBindZoneHash, &RenderPipelineBindZoneEql);
    }
    void *RenderPipeline::GetOrCreateFor(AstralCanvas::RenderProgram *renderProgram, u32 renderPassToUse)
    {
        RenderPipelineBindZone bindZone;
        bindZone.renderProgramHandle = renderProgram->handle;
        bindZone.subPassHandle = renderPassToUse;
        void *handle = this->zoneToPipelineInstance.GetCopyOr(bindZone, NULL);
        if (handle != NULL)
        {
            return handle;
        }

        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                RenderPipeline *pipeline = this;

                IAllocator cAllocator = GetCAllocator();
                ArenaAllocator arena = ArenaAllocator(&cAllocator);

                const i32 dynamicStateCount = 2;
                collections::Array<VkDynamicState> dynamicStates = collections::Array<VkDynamicState>(&arena.asAllocator, dynamicStateCount);
                dynamicStates.data[0] = VK_DYNAMIC_STATE_VIEWPORT;
                dynamicStates.data[1] = VK_DYNAMIC_STATE_SCISSOR;

                VkPipelineDynamicStateCreateInfo dynamicState{};
                dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.dynamicStateCount = dynamicStateCount;
                dynamicState.pDynamicStates = dynamicStates.data;

                //vertex declarations

                usize vertexDeclCount = pipeline->vertexDeclarations.length;
                VkPipelineVertexInputStateCreateInfo vertexInputInfo{};

                if (vertexDeclCount > 0)
                {
                    usize attribCount = 0;
                    collections::Array<AstralVulkanVertexDecl> vertexDecls = collections::Array<AstralVulkanVertexDecl>(&arena.asAllocator, vertexDeclCount);
                    for (usize i = 0; i < vertexDeclCount; i++)
                    {
                        vertexDecls.data[i] = AstralCanvasVk_CreateVertexDecl(&arena.asAllocator, &pipeline->vertexDeclarations.data[i]);
                        attribCount += vertexDecls.data[i].attributeDescriptions.length;
                    }

                    collections::Array<VkVertexInputBindingDescription> bindingDescriptions = collections::Array<VkVertexInputBindingDescription>(&arena.asAllocator, vertexDeclCount);
                    for (usize i = 0; i < vertexDeclCount; i++)
                    {
                        bindingDescriptions.data[i] = vertexDecls.data[i].bindingDescription;
                    }

                    collections::Array<VkVertexInputAttributeDescription> attribDescriptions = collections::Array<VkVertexInputAttributeDescription>(&arena.asAllocator, attribCount);
                    usize attribIndex = 0;
                    for (usize i = 0; i < vertexDeclCount; i++)
                    {
                        for (usize j = 0; j < vertexDecls.data[i].attributeDescriptions.length; j++)
                        {
                            attribDescriptions.data[attribIndex] = vertexDecls.data[i].attributeDescriptions.data[j];
                            attribIndex++;
                        }
                    }

                    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                    vertexInputInfo.vertexBindingDescriptionCount = vertexDeclCount;
                    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data;
                    vertexInputInfo.vertexAttributeDescriptionCount = attribCount;
                    vertexInputInfo.pVertexAttributeDescriptions = attribDescriptions.data;
                }

                //primitive type

                VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
                inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssemblyInfo.topology = AstralCanvasVk_FromPrimitiveType(pipeline->primitiveType);
                inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

                //viewport data

                VkPipelineViewportStateCreateInfo viewportStateInfo{};
                viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportStateInfo.viewportCount = 1;
                viewportStateInfo.scissorCount = 1;

                //rasterization behaviour

                VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
                rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizerInfo.depthClampEnable = false;
                rasterizerInfo.rasterizerDiscardEnable = false;
                rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
                if (inputAssemblyInfo.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST || inputAssemblyInfo.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP)
                {
                    rasterizerInfo.polygonMode = VK_POLYGON_MODE_LINE;
                }
                rasterizerInfo.lineWidth = 1.0f;
                rasterizerInfo.cullMode = AstralCanvasVk_FromCullMode(pipeline->cullMode);
                rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

                rasterizerInfo.depthBiasEnable = false;
                rasterizerInfo.depthBiasConstantFactor = 0.0f;
                rasterizerInfo.depthBiasClamp = 0.0f;
                rasterizerInfo.depthBiasSlopeFactor = 0.0f;

                //multisampling data
                //todo

                VkPipelineMultisampleStateCreateInfo multisamplingInfo{};
                multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisamplingInfo.sampleShadingEnable = false;
                multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                multisamplingInfo.minSampleShading = 1.0f;
                multisamplingInfo.pSampleMask = nullptr;
                multisamplingInfo.alphaToCoverageEnable = false;
                multisamplingInfo.alphaToOneEnable = false;

                //depth stencil data
                
                VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
                depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencilInfo.depthTestEnable = pipeline->depthTest;
                depthStencilInfo.depthWriteEnable = pipeline->depthWrite;
                depthStencilInfo.depthCompareOp = pipeline->depthTest ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_NEVER;
                depthStencilInfo.depthBoundsTestEnable = false;
                depthStencilInfo.minDepthBounds = 0.0f;
                depthStencilInfo.maxDepthBounds = 1.0f;
                depthStencilInfo.stencilTestEnable = false;

                VkPipelineColorBlendAttachmentState colorBlendState {};
                colorBlendState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // ColorComponentFlags.RBit | ColorComponentFlags.GBit | ColorComponentFlags.BBit | ColorComponentFlags.ABit;
                if (pipeline->blendState != NULL)
                {
                    colorBlendState.srcColorBlendFactor = AstralCanvasVk_FromBlend(pipeline->blendState->sourceColorBlend);
                    colorBlendState.srcAlphaBlendFactor = AstralCanvasVk_FromBlend(pipeline->blendState->sourceAlphaBlend);
                    colorBlendState.dstColorBlendFactor = AstralCanvasVk_FromBlend(pipeline->blendState->destinationColorBlend);
                    colorBlendState.dstAlphaBlendFactor = AstralCanvasVk_FromBlend(pipeline->blendState->destinationAlphaBlend);
                    colorBlendState.colorBlendOp = VK_BLEND_OP_ADD;
                    colorBlendState.alphaBlendOp = VK_BLEND_OP_ADD;
                    colorBlendState.blendEnable = true;
                }
                else
                    colorBlendState.blendEnable = false;

                //color blend data

                VkPipelineColorBlendStateCreateInfo colorBlendInfo {};
                colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlendInfo.blendConstants[0] = 1.0f;
                colorBlendInfo.blendConstants[1] = 1.0f;
                colorBlendInfo.blendConstants[2] = 1.0f;
                colorBlendInfo.blendConstants[3] = 1.0f;
                colorBlendInfo.logicOpEnable = false;
                colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;
                colorBlendInfo.attachmentCount = 1;
                colorBlendInfo.pAttachments = &colorBlendState;

                //pipeline layout itself

                VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
                pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
                pipelineLayoutCreateInfo.pPushConstantRanges = NULL;
                pipelineLayoutCreateInfo.flags = 0;

                //u32 descriptorsCount = pipeline->shader->shaderVariables.samplers.length + pipeline->shader->shaderVariables.textures.length + pipeline->shader->shaderVariables.uniforms.length;
                pipelineLayoutCreateInfo.setLayoutCount = 0;
                if (pipeline->shader->shaderPipelineLayout != NULL)
                {
                    pipelineLayoutCreateInfo.setLayoutCount = 1;
                    pipelineLayoutCreateInfo.pSetLayouts = (VkDescriptorSetLayout*)&pipeline->shader->shaderPipelineLayout;
                }

                if (vkCreatePipelineLayout(AstralCanvasVk_GetCurrentGPU()->logicalDevice, &pipelineLayoutCreateInfo, NULL, (VkPipelineLayout*)&pipeline->layout) != VK_SUCCESS)
                {
                    arena.deinit();
                    break;
                }

                //pipeline itself

                VkPipelineShaderStageCreateInfo shaderStageInfos[2] = {{}, {}};
                shaderStageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                shaderStageInfos[0].module = (VkShaderModule)pipeline->shader->shaderModule1;
                shaderStageInfos[0].pName = "main"; //entry point

                shaderStageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                shaderStageInfos[1].module = (VkShaderModule)pipeline->shader->shaderModule2;
                shaderStageInfos[1].pName = "main"; //entry point

                VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
                pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineCreateInfo.pStages = shaderStageInfos;
                pipelineCreateInfo.stageCount = 2;
                pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
                pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
                pipelineCreateInfo.pViewportState = &viewportStateInfo;
                pipelineCreateInfo.pRasterizationState = &rasterizerInfo;
                pipelineCreateInfo.pMultisampleState = &multisamplingInfo;
                pipelineCreateInfo.pColorBlendState = &colorBlendInfo;
                pipelineCreateInfo.pDepthStencilState = &depthStencilInfo;
                pipelineCreateInfo.pDynamicState = &dynamicState;
                pipelineCreateInfo.layout = (VkPipelineLayout)pipeline->layout;
                pipelineCreateInfo.renderPass = (VkRenderPass)renderProgram->handle;
                pipelineCreateInfo.subpass = renderPassToUse;

                VkPipeline result;
                vkCreateGraphicsPipelines(AstralCanvasVk_GetCurrentGPU()->logicalDevice, NULL, 1, &pipelineCreateInfo, NULL, &result);

                arena.deinit();

                return result;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend");
                break;
        }
        return NULL;
    }
    void RenderPipeline::deinit()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                for (usize i = 0; i < this->zoneToPipelineInstance.bucketsCount; i++)
                {
                    if (this->zoneToPipelineInstance.buckets[i].initialized)
                    {
                        for (usize j = 0; j < this->zoneToPipelineInstance.buckets[i].entries.count; j++)
                        {
                            VkPipeline vkPipeline = (VkPipeline)this->zoneToPipelineInstance.buckets[i].entries.ptr[j].value;
                            vkDestroyPipeline(AstralCanvasVk_GetCurrentGPU()->logicalDevice, vkPipeline, NULL);
                        }
                    }
                }
                this->zoneToPipelineInstance.deinit();
                break;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend");
                break;
        }
    }
}