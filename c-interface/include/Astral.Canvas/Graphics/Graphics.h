#pragma once
#include "Linxc.h"
#include "Astral.Canvas/Graphics/IndexBuffer.h"
#include "Astral.Canvas/Graphics/VertexBuffer.h"
#include "Astral.Canvas/Graphics/RenderTarget.h"
#include "Astral.Canvas/Graphics/Texture2D.h"
#include "Astral.Canvas/Graphics/SamplerState.h"
#include "Astral.Canvas/Graphics/RenderProgram.h"
#include "Astral.Canvas/Graphics/RenderPipeline.h"
#include "Astral.Canvas/Graphics/Color.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void *AstralCanvasGraphics;

    DynamicFunction AstralCanvasRenderProgram AstralCanvasGraphics_GetCurrentRenderProgram(AstralCanvasGraphics ptr);
    DynamicFunction AstralCanvasRenderTarget AstralCanvasGraphics_GetCurrentRenderTarget(AstralCanvasGraphics ptr);
    DynamicFunction u32 AstralCanvasGraphics_GetCurrentRenderProgramPass(AstralCanvasGraphics ptr);
    DynamicFunction void AstralCanvasGraphics_SetVertexBuffer(AstralCanvasGraphics ptr, const AstralCanvasVertexBuffer vb, u32 bindingPoint);
    DynamicFunction void AstralCanvasGraphics_SetIndexBuffer(AstralCanvasGraphics ptr, const AstralCanvasIndexBuffer indexBuffer);
    DynamicFunction void AstralCanvasGraphics_SetRenderTarget(AstralCanvasGraphics ptr, AstralCanvasRenderTarget target);
    DynamicFunction void AstralCanvasGraphics_StartRenderProgram(AstralCanvasGraphics ptr, AstralCanvasRenderProgram program, AstralCanvasColor clearColor);
    DynamicFunction void AstralCanvasGraphics_EndRenderProgram(AstralCanvasGraphics ptr);
    DynamicFunction void AstralCanvasGraphics_UseRenderPipeline(AstralCanvasGraphics ptr, AstralCanvasRenderPipeline pipeline);
    DynamicFunction void AstralCanvasGraphics_AwaitGraphicsIdle(AstralCanvasGraphics ptr);
    DynamicFunction void AstralCanvasGraphics_SetShaderVariable(AstralCanvasGraphics ptr, const char* variableName, void* data, usize size);
    DynamicFunction void AstralCanvasGraphics_SetShaderVariableTexture(AstralCanvasGraphics ptr, const char* variableName, AstralCanvasTexture2D texture);
    DynamicFunction void AstralCanvasGraphics_SetShaderVariableTextures(AstralCanvasGraphics ptr, const char* variableName, AstralCanvasTexture2D *textures, usize count);
    DynamicFunction void AstralCanvasGraphics_SetShaderVariableSampler(AstralCanvasGraphics ptr, const char* variableName, AstralCanvasSamplerState sampler);
    DynamicFunction void AstralCanvasGraphics_SetShaderVariableSamplers(AstralCanvasGraphics ptr, const char* variableName, AstralCanvasSamplerState *samplers, usize count);
    DynamicFunction void AstralCanvasGraphics_SendUpdatedUniforms(AstralCanvasGraphics ptr);
    DynamicFunction void AstralCanvasGraphics_DrawIndexedPrimitives(AstralCanvasGraphics ptr, u32 indexCount, u32 instanceCount, u32 firstIndex, u32 vertexOffset, u32 firstInstance);

#ifdef __cplusplus
}
#endif