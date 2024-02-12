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

#ifdef MACOS
#include "Graphics/Metal/MetalImplementations.h"
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
                    name.deinit();
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
                    //printf("at %u is null?: %s\n", binding, results->uniforms.Get(binding) == NULL ? "true" : "false");
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
                    name.deinit();
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
                    //printf("at %u is null?: %s\n", binding, results->uniforms.Get(binding) == NULL ? "true" : "false");
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
                    name.deinit();
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
                    //printf("at %u is null?: %s\n", binding, results->uniforms.Get(binding) == NULL ? "true" : "false");
                }
            }
        }
        JsonElement *inputAttachments = json->GetProperty("inputAttachments");
        if (inputAttachments != NULL)
        {
            for (usize i = 0; i < inputAttachments->arrayElements.length; i++)
            {
                string name = inputAttachments->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 index = inputAttachments->arrayElements.data[i].GetProperty("index")->GetUint32();
                u32 set = inputAttachments->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = inputAttachments->arrayElements.data[i].GetProperty("binding")->GetUint32();

                ShaderResource *resource = results->uniforms.Get(binding);
                if (resource != NULL && resource->variableName.buffer != NULL)
                {
                    resource->accessedBy = (ShaderInputAccessedBy)((u32)resource->accessedBy | (u32)accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    //printf("Added input attachment: %s at binding %u, index %u\n", name.buffer, binding, index);
                    ShaderResource newResource;
                    newResource.binding = binding;
                    newResource.set = set;
                    newResource.variableName = name;
                    newResource.arrayLength = 0;
                    newResource.inputAttachmentIndex = index;
                    newResource.accessedBy = accessedByShaderOfType;
                    newResource.type = ShaderResourceType_InputAttachment;
                    newResource.size = 0;
                    newResource.stagingData = collections::vector<ShaderStagingMutableState>(results->allocator);
                    results->uniforms.Insert((usize)binding, newResource);
                    //printf("at %u is null?: %s\n", binding, results->uniforms.Get(binding) == NULL ? "true" : "false");
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
                return (i32)i;
            }
        }
        return -1;
    }
    void Shader::CheckDescriptorSetAvailability(bool forceAddNewDescriptor)
    {
        if (descriptorForThisDrawCall >= descriptorSets.count || forceAddNewDescriptor)
        {
            switch (GetActiveBackend())
            {
    #ifdef ASTRALCANVAS_VULKAN
                case Backend_Vulkan:
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
                                newMutableState.ownsUniformBuffer = true;
                                break;
                            }
                            case ShaderResourceType_InputAttachment:
                            {
                                newMutableState.textures = collections::Array<Texture2D*>(this->allocator, 1);
                                newMutableState.imageInfos = this->allocator->Allocate(sizeof(VkDescriptorImageInfo) * newMutableState.textures.length);
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
                        shaderVariables.uniforms.ptr[i].stagingData.Add(newMutableState);
                    }
                    break;
                }
    #endif
    #ifdef ASTRALCANVAS_METAL
            case Backend_Metal:
                {
                    AstralCanvasMetal_AddUniformDescriptorSets(this);
                    break;
                }
    #endif
            default:
                break;
            }
        }
    }
    void Shader::SyncUniformsWithGPU(void *commandEncoder)
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
                        case ShaderResourceType_InputAttachment:
                        {
                            VkDescriptorImageInfo imageInfo{};
                            imageInfo.sampler = NULL;
                            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; //(VkImageLayout)toMutate->textures.data[i]->imageLayout;
                            imageInfo.imageView = (VkImageView)toMutate->textures.data[0]->imageView;
                            ((VkDescriptorImageInfo*)toMutate->imageInfos)[0] = imageInfo;

                            setWrite.dstArrayElement = 0;
                            setWrite.descriptorCount = toMutate->textures.length;
                            setWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                            setWrite.pImageInfo = (VkDescriptorImageInfo*)toMutate->imageInfos;
                            break;
                        }
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
                        default:
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
#ifdef ASTRALCANVAS_METAL
            case Backend_Metal:
            {
                AstralCanvasMetal_SyncUniformsWithGPU(commandEncoder, this);
                
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
#ifdef ASTRALCANVAS_METAL
            case Backend_Metal:
            {
                AstralCanvasMetal_DestroyShaderProgram(this->shaderModule1, this->shaderModule2);
                this->shaderModule1 = NULL;
                this->shaderModule2 = NULL;
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
        *result = AstralCanvas::Shader(allocator, shaderType);
        ArenaAllocator localArena = ArenaAllocator(allocator);
        
        JsonElement root;
        usize parseJsonResult = ParseJsonDocument(&localArena.asAllocator, jsonString, &root);
        if (parseJsonResult != 0)
        {
            localArena.deinit();
            return (i32)parseJsonResult;
        }
        
        switch (GetActiveBackend())
        {
            #ifdef ASTRALCANVAS_VULKAN
            case Backend_Vulkan:
            {

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
                                //printf("resource %s at binding %u of descriptor type %i, count %u with stage flags %i\n", resource.variableName.buffer, resource.binding, layoutBinding.descriptorType, layoutBinding.descriptorCount, layoutBinding.stageFlags);
                            }
                            layoutInfo.pBindings = bindings.data;

                            if (vkCreateDescriptorSetLayout(AstralCanvasVk_GetCurrentGPU()->logicalDevice, &layoutInfo, NULL, (VkDescriptorSetLayout*)&result->shaderPipelineLayout) != VK_SUCCESS)
                            {
                                fprintf(stderr, "Error creating descriptor set layout\n");
                            }
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
#ifdef ASTRALCANVAS_METAL
            case Backend_Metal:
            {
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
                        
                        JsonElement *vertexMetal = vertexElement->GetProperty("msl");
                        JsonElement *fragmentMetal = fragmentElement->GetProperty("msl");
                        
                        string vertexMetalString = vertexMetal->GetString(&localArena.asAllocator);
                        string fragmentMetalString = fragmentMetal->GetString(&localArena.asAllocator);
                        
                        if (!AstralCanvasMetal_CreateShaderProgram(vertexMetalString, fragmentMetalString, &result->shaderModule1, &result->shaderModule2))
                        {
                            THROW_ERR("Failed to create metal shader program!");
                        }
                    }
                }
                return 0;
            }
#endif
            default:
                THROW_ERR("Unimplemented backend: Shader CreateShaderFromString");
                break;
        }
        localArena.deinit();
        return -1;
    }
}
