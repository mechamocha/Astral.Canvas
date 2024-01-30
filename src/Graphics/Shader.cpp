#include "Graphics/Shader.hpp"
#include "Graphics/CurrentBackend.hpp"
#include "ArenaAllocator.hpp"
#include "ErrorHandling.hpp"
#include "Json.hpp"
#include "cmath"

#ifdef ASTRALCANVAS_VULKAN
#include "Graphics/Vulkan/VulkanInstanceData.hpp"
#include "Graphics/Vulkan/VulkanEnumConverters.hpp"
#endif

using namespace SomnialJson;

namespace AstralCanvas
{
    Shader::Shader()
    {
        this->allocator = NULL;
        shaderType = ShaderType_VertexFragment;
        shaderModule1 = NULL;
        shaderModule2 = NULL;
        shaderPipelineLayout = NULL;
        shaderVariables = ShaderVariables();
        uniformsHasBeenSet = false;

        this->descriptorForThisDrawCall = 0;
        this->descriptorSets = collections::vector<void *>();
    }
    Shader::Shader(IAllocator *allocator, ShaderType type)
    {
        this->allocator = allocator;
        shaderType = type;
        shaderModule1 = NULL;
        shaderModule2 = NULL;
        shaderPipelineLayout = NULL;
        shaderVariables = ShaderVariables(allocator);
        uniformsHasBeenSet = false;

        this->descriptorForThisDrawCall = 0;
        this->descriptorSets = collections::vector<void *>(allocator);
    }
    void ParseShaderVariables(JsonElement *json, ShaderVariables *results, ShaderInputAccessedBy accessedByShaderOfType)
    {
        JsonElement *uniforms = json->GetProperty("uniforms");
        if (uniforms != NULL)
        {
            for (usize i = 0; i < uniforms->arrayElements.length; i++)
            {
                string name = uniforms->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 stride = uniforms->arrayElements.data[i].GetProperty("stride")->GetUint32();
                u32 set = uniforms->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = uniforms->arrayElements.data[i].GetProperty("binding")->GetUint32();

                ShaderResource *resource = results->uniforms.Get(binding);
                if (resource != NULL && resource->variableName.buffer != NULL)
                {
                    resource->accessedBy = (ShaderInputAccessedBy)((u32)resource->accessedBy | (u32)accessedByShaderOfType);
                }
                else
                {
                    ShaderResource newResource{};
                    newResource.binding = binding;
                    newResource.set = set;
                    newResource.variableName = name;
                    newResource.size = stride;
                    newResource.accessedBy = accessedByShaderOfType;
                    newResource.arrayLength = 0;
                    newResource.stagingData = collections::vector<ShaderStagingMutableState>(results->allocator);
                    newResource.type = ShaderResourceType_Uniform;
                    results->uniforms.Insert((usize)binding, newResource);
                }
                //results->uniforms.Add(binding, {name, set, binding, stride});
            }
        }
        JsonElement *textures = json->GetProperty("images");
        if (textures != NULL)
        {
            for (usize i = 0; i < textures->arrayElements.length; i++)
            {
                string name = textures->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 arrayLength = textures->arrayElements.data[i].GetProperty("arrayLength")->GetUint32();
                u32 set = textures->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = textures->arrayElements.data[i].GetProperty("binding")->GetUint32();

                ShaderResource *resource = results->uniforms.Get(binding);
                if (resource != NULL && resource->variableName.buffer != NULL)
                {
                    resource->accessedBy = (ShaderInputAccessedBy)((u32)resource->accessedBy | (u32)accessedByShaderOfType);
                }
                else
                {
                    ShaderResource newResource;
                    newResource.binding = binding;
                    newResource.set = set;
                    newResource.variableName = name;
                    newResource.arrayLength = arrayLength;
                    newResource.accessedBy = accessedByShaderOfType;
                    newResource.type = ShaderResourceType_Texture;
                    newResource.size = 0;
                    newResource.stagingData = collections::vector<ShaderStagingMutableState>(results->allocator);
                    results->uniforms.Insert((usize)binding, newResource);
                }
            }
        }
        JsonElement *samplers = json->GetProperty("samplers");
        if (samplers != NULL)
        {
            for (usize i = 0; i < samplers->arrayElements.length; i++)
            {
                string name = samplers->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 arrayLength = samplers->arrayElements.data[i].GetProperty("arrayLength")->GetUint32();
                u32 set = samplers->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = samplers->arrayElements.data[i].GetProperty("binding")->GetUint32();

                ShaderResource *resource = results->uniforms.Get(binding);
                if (resource != NULL && resource->variableName.buffer != NULL)
                {
                    resource->accessedBy = (ShaderInputAccessedBy)((u32)resource->accessedBy | (u32)accessedByShaderOfType);
                }
                else
                {
                    ShaderResource newResource;
                    newResource.binding = binding;
                    newResource.set = set;
                    newResource.variableName = name;
                    newResource.arrayLength = arrayLength;
                    newResource.accessedBy = accessedByShaderOfType;
                    newResource.type = ShaderResourceType_Sampler;
                    newResource.size = 0;
                    newResource.stagingData = collections::vector<ShaderStagingMutableState>(results->allocator);
                    results->uniforms.Insert((usize)binding, newResource);
                }
            }
        }
    }
    i32 Shader::GetVariableBinding(const char* variableName)
    {
        for (usize i = 0; i < this->shaderVariables.uniforms.capacity; i++)
        {
            if (this->shaderVariables.uniforms.ptr[i].variableName.buffer == NULL)
            {
                break;
            }
            if (this->shaderVariables.uniforms.ptr[i].variableName == variableName)
            {
                return i;
            }
        }
        return -1;
    }
    void Shader::CheckDescriptorSetAvailability()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                if (descriptorForThisDrawCall >= descriptorSets.count)
                {
                    VkDescriptorPool descriptorPool = AstralCanvasVk_GetDescriptorPool();
                    VkDescriptorSet result;
                    VkDescriptorSetAllocateInfo allocInfo{};
                    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                    allocInfo.descriptorSetCount = 1;
                    allocInfo.descriptorPool = descriptorPool;
                    allocInfo.pSetLayouts = (VkDescriptorSetLayout*)&this->shaderPipelineLayout;
                
                    if (vkAllocateDescriptorSets(AstralCanvasVk_GetCurrentGPU()->logicalDevice, &allocInfo, &result) != VK_SUCCESS)
                    {
                        THROW_ERR("Error creating descriptor set!");
                    }
                    descriptorSets.Add(result);
                    for (usize i = 0; i < shaderVariables.uniforms.capacity; i++)
                    {
                        ShaderResource *resource = &shaderVariables.uniforms.ptr[i];
                        if (resource->variableName.buffer == NULL)
                        {
                            break;
                        }
                        ShaderStagingMutableState newMutableState{};
                        switch (resource->type)
                        {
                            case ShaderResourceType_Uniform:
                            {
                                newMutableState.ub = UniformBuffer(resource->size);
                                break;
                            }
                            case ShaderResourceType_Texture:
                            {
                                newMutableState.textures = collections::Array<Texture2D*>(this->allocator, max(resource->arrayLength, 1));
                                newMutableState.imageInfos = this->allocator->Allocate(sizeof(VkDescriptorImageInfo) * newMutableState.textures.length);
                                break;
                            }
                            case ShaderResourceType_Sampler:
                            {
                                newMutableState.samplers = collections::Array<SamplerState*>(this->allocator, max(resource->arrayLength, 1));
                                newMutableState.samplerInfos = this->allocator->Allocate(sizeof(VkDescriptorImageInfo) * newMutableState.samplers.length);
                                break;
                            }
                        }
                        printf("Created new mutable state\n");
                        shaderVariables.uniforms.ptr[i].stagingData.Add(newMutableState);
                    }
                }
                break;
            }
            #endif
            default:
                break;
        }
    }
    void Shader::SyncUniformsWithGPU()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                u32 setWriteCount = 0;
                u32 bufferInfoCount = 0;
                VkWriteDescriptorSet setWrites[MAX_UNIFORMS_IN_SHADER];
                VkDescriptorBufferInfo bufferInfos[MAX_UNIFORMS_IN_SHADER];

                for (usize i = 0; i < this->shaderVariables.uniforms.capacity; i++)
                {
                    if (this->shaderVariables.uniforms.ptr[i].variableName.buffer == NULL)
                    {
                        break;
                    }
                    //should never throw out of range error since CheckDescriptorSetAvailability() is always called prior to this
                    ShaderStagingMutableState *toMutate = &this->shaderVariables.uniforms.ptr[i].stagingData.ptr[this->descriptorForThisDrawCall];
                    if (!toMutate->mutated)
                    {
                        continue;
                    }
                    //set mutated to false in anticipation of reuse for next frame
                    toMutate->mutated = false;

                    VkWriteDescriptorSet setWrite{};
                    setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    setWrite.dstSet = (VkDescriptorSet)this->descriptorSets.ptr[descriptorForThisDrawCall];
                    setWrite.dstBinding = this->shaderVariables.uniforms.ptr[i].binding;

                    switch (this->shaderVariables.uniforms.ptr[i].type)
                    {
                        case ShaderResourceType_Uniform:
                        {
                            UniformBuffer buffer = toMutate->ub;
                            bufferInfos[bufferInfoCount].buffer = (VkBuffer)buffer.handle;
                            bufferInfos[bufferInfoCount].offset = 0;
                            bufferInfos[bufferInfoCount].range = buffer.size;

                            setWrite.dstArrayElement = 0;
                            setWrite.descriptorCount = 1;
                            setWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                            setWrite.pBufferInfo = &bufferInfos[bufferInfoCount];

                            bufferInfoCount += 1;

                            break;
                        }
                        case ShaderResourceType_Texture:
                        {
                            for (usize i = 0; i < toMutate->textures.length; i++)
                            {
                                VkDescriptorImageInfo imageInfo{};
                                imageInfo.sampler = NULL;
                                //printf("\nimage layout: %u\n\n", toMutate->textures.data[i]->imageLayout);
                                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //(VkImageLayout)toMutate->textures.data[i]->imageLayout;
                                imageInfo.imageView = (VkImageView)toMutate->textures.data[i]->imageView;
                                ((VkDescriptorImageInfo*)toMutate->imageInfos)[i] = imageInfo;
                            }

                            setWrite.dstArrayElement = 0;
                            setWrite.descriptorCount = toMutate->textures.length;
                            setWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                            setWrite.pImageInfo = (VkDescriptorImageInfo*)toMutate->imageInfos;

                            break;
                        }
                        case ShaderResourceType_Sampler:
                        {
                            for (usize i = 0; i < toMutate->samplers.length; i++)
                            {
                                VkDescriptorImageInfo samplerInfo{};
                                samplerInfo.sampler = (VkSampler)toMutate->samplers.data[i]->handle;
                                samplerInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                                samplerInfo.imageView = NULL;
                                ((VkDescriptorImageInfo*)toMutate->samplerInfos)[i] = samplerInfo;
                            }

                            setWrite.dstArrayElement = 0;
                            setWrite.descriptorCount = toMutate->samplers.length;
                            setWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                            setWrite.pImageInfo = (VkDescriptorImageInfo*)toMutate->samplerInfos;

                            break;
                        }
                        case ShaderResourceType_StructuredBuffer:
                        {
                            break;
                        }
                    }
                    setWrites[setWriteCount] = setWrite;
                    setWriteCount += 1;
                }

                vkUpdateDescriptorSets(AstralCanvasVk_GetCurrentGPU()->logicalDevice, setWriteCount, setWrites, 0, NULL);

                break;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend: Shader SyncUniformsWithGPU");
                break;
        }
    }
    void Shader::deinit()
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                VkDevice logicalDevice = AstralCanvasVk_GetCurrentGPU()->logicalDevice;

                this->shaderVariables.deinit();
                if (this->shaderPipelineLayout != NULL)
                {
                    vkDestroyDescriptorSetLayout(logicalDevice, (VkDescriptorSetLayout)this->shaderPipelineLayout, NULL);
                }
                if (this->shaderModule1 != NULL)
                {
                    vkDestroyShaderModule(logicalDevice, (VkShaderModule)this->shaderModule1, NULL);
                }
                if (this->shaderModule2 != NULL)
                {
                    vkDestroyShaderModule(logicalDevice, (VkShaderModule)this->shaderModule2, NULL);
                }
                break;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend: Shader deinit");
                break;
        }

        this->shaderVariables.deinit();
    }
    i32 CreateShaderFromString(IAllocator *allocator, ShaderType shaderType, string jsonString, Shader* result)
    {
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {
                *result = AstralCanvas::Shader(allocator, shaderType);
                ArenaAllocator localArena = ArenaAllocator(allocator);
                
                JsonElement root;
                usize parseJsonResult = ParseJsonDocument(&localArena.asAllocator, jsonString, &root);
                if (parseJsonResult != 0)
                {
                    localArena.deinit();
                    return (i32)parseJsonResult;
                }

                JsonElement *computeElement = root.GetProperty("compute");

                if (computeElement != NULL)
                {
                    ParseShaderVariables(computeElement, &result->shaderVariables, InputAccessedBy_Compute);
                }
                else
                {
                    JsonElement *vertexElement = root.GetProperty("vertex");
                    JsonElement *fragmentElement = root.GetProperty("fragment");

                    if (vertexElement != NULL && fragmentElement != NULL)
                    {
                        ParseShaderVariables(vertexElement, &result->shaderVariables, InputAccessedBy_Vertex);
                        ParseShaderVariables(fragmentElement, &result->shaderVariables, InputAccessedBy_Fragment);

                        JsonElement *vertexSpirv = vertexElement->GetProperty("spirv");
                        JsonElement *fragmentSpirv = fragmentElement->GetProperty("spirv");

                        collections::Array<u32> vertexSpirvData = collections::Array<u32>(&localArena.asAllocator, vertexSpirv->arrayElements.length);
                        collections::Array<u32> fragmentSpirvData = collections::Array<u32>(&localArena.asAllocator, fragmentSpirv->arrayElements.length);

                        for (usize i = 0; i < vertexSpirv->arrayElements.length; i++)
                        {
                            vertexSpirvData.data[i] = vertexSpirv->arrayElements.data[i].GetUint32();
                        }
                        for (usize i = 0; i < fragmentSpirv->arrayElements.length; i++)
                        {
                            fragmentSpirvData.data[i] = fragmentSpirv->arrayElements.data[i].GetUint32();
                        }

                        VkDevice logicalDevice = AstralCanvasVk_GetCurrentGPU()->logicalDevice;

                        VkShaderModuleCreateInfo vertexCreateInfo = {};
                        vertexCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                        vertexCreateInfo.codeSize = vertexSpirvData.length * 4;
                        vertexCreateInfo.pCode = vertexSpirvData.data;

                        VkShaderModule vertexShaderModule;
                        if (vkCreateShaderModule(logicalDevice, &vertexCreateInfo, NULL, &vertexShaderModule) != VK_SUCCESS)
                        {
                            localArena.deinit();
                            return -1;
                        }

                        VkShaderModuleCreateInfo fragmentCreateInfo = {};
                        fragmentCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                        fragmentCreateInfo.codeSize = fragmentSpirvData.length * 4;
                        fragmentCreateInfo.pCode = fragmentSpirvData.data;

                        VkShaderModule fragmentShaderModule;
                        if (vkCreateShaderModule(logicalDevice, &fragmentCreateInfo, NULL, &fragmentShaderModule) != VK_SUCCESS)
                        {
                            localArena.deinit();
                            return -1;
                        }

                        result->shaderModule1 = vertexShaderModule;
                        result->shaderModule2 = fragmentShaderModule;

                        //create descriptor
                        VkDescriptorSetLayoutCreateInfo layoutInfo{};
                        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                        layoutInfo.flags = 0;
                        layoutInfo.bindingCount = 0;

                        for (usize i = 0; i < result->shaderVariables.uniforms.capacity; i++)
                        {
                            if (result->shaderVariables.uniforms.ptr[i].variableName.buffer == NULL)
                            {
                                break;
                            }
                            layoutInfo.bindingCount++;
                        }

                        result->shaderPipelineLayout = NULL;
                        printf("Layout binding count: %u\n", layoutInfo.bindingCount);
                        if (layoutInfo.bindingCount > 0)
                        {
                            collections::Array<VkDescriptorSetLayoutBinding> bindings = collections::Array<VkDescriptorSetLayoutBinding>(&localArena.asAllocator, layoutInfo.bindingCount);

                            for (usize i = 0; i < result->shaderVariables.uniforms.capacity; i++)
                            {
                                if (result->shaderVariables.uniforms.ptr[i].variableName.buffer == NULL)
                                {
                                    break;
                                }

                                ShaderResource resource = result->shaderVariables.uniforms.ptr[i];
                                VkDescriptorSetLayoutBinding layoutBinding = {};
                                layoutBinding.binding = resource.binding;
                                layoutBinding.descriptorCount = max(resource.arrayLength, 1);
                                layoutBinding.descriptorType = AstralCanvasVk_FromResourceType(resource.type);
                                layoutBinding.stageFlags = AstralCanvasVk_FromAccessedBy(resource.accessedBy);
                                layoutBinding.pImmutableSamplers = NULL;

                                bindings.data[resource.binding] = layoutBinding;
                            }
                            layoutInfo.pBindings = bindings.data;

                            vkCreateDescriptorSetLayout(AstralCanvasVk_GetCurrentGPU()->logicalDevice, &layoutInfo, NULL, (VkDescriptorSetLayout*)&result->shaderPipelineLayout);
                        }
                    }
                    else
                    {
                        localArena.deinit();
                        return -1;
                    }
                }

                localArena.deinit();
                return 0;
            }
            #endif
            default:
                THROW_ERR("Unimplemented backend: Shader CreateShaderFromString");
                break;
        }
        return -1;
    }
}