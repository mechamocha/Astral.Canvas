#include "Graphics/Vulkan/VulkanVertex.hpp"
#include "Graphics/Vulkan/VulkanEnumConverters.hpp"

AstralVulkanVertexDecl AstralCanvasVk_CreateVertexDecl(IAllocator *allocator, AstralCanvas::VertexDeclaration *vertexDecl)
{
    AstralVulkanVertexDecl result = {};
    result.bindingDescription.binding = 0;
    result.bindingDescription.inputRate = (VkVertexInputRate)vertexDecl->inputRate;
    result.bindingDescription.stride = vertexDecl->size;
    result.attributeDescriptions = collections::Array<VkVertexInputAttributeDescription>(allocator, vertexDecl->elements.count);

    for (usize i = 0; i < result.attributeDescriptions.length; i++)
    {
        VkVertexInputAttributeDescription *attrib = &result.attributeDescriptions.data[i];
        attrib->binding = (u32)i;
        attrib->format = AstralCanvasVk_FromVertexElementFormat(vertexDecl->elements.ptr[i].format);
        attrib->location = 0;
        attrib->offset = vertexDecl->elements.ptr[i].offset;
    }

    return result;
}
void AstralCanvasVk_DestroyVertexDecl(AstralVulkanVertexDecl *vertexDecl)
{
    vertexDecl->attributeDescriptions.deinit();
}