#ifdef ASTRALCANVAS_WGPU

#include "Graphics/WGPU/WgpuEngine.hpp"
#include "ErrorHandling.hpp"
#include "io.hpp"

#ifdef WINDOWS
#include "Windows.h"
#endif

#include "GLFW/glfw3native.h"

using namespace collections;

AstralCanvasWgpu_Engine engineInstance;

AstralCanvasWgpu_Engine* AstralCanvasWgpu_GetEngineInstance()
{
    return &engineInstance;
}

void AstralCanvasWgpu_LogCallback(WGPULogLevel level, char const *message, void *userdata)
{
    if (level == WGPULogLevel_Error)
    {
        THROW_ERR(message);
    }
    else AstralDebugger_Log(message);
}

void AstralCanvasWgpu_InitLogging(WGPULogLevel logLevel)
{
    wgpuSetLogCallback(&AstralCanvasWgpu_LogCallback, NULL);
    wgpuSetLogLevel(logLevel);
}

void AstralCanvasWgpu_HandleRequestedAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *userdata) 
{
    if (status == WGPURequestAdapterStatus_Success) 
    {
        engineInstance.adapter = adapter;
    }
    else
    {
        AstralDebugger_Log(message);
    }
}
void AstralCanvasWgpu_HandleRequestedDevice(WGPURequestDeviceStatus status, WGPUDevice device, char const *message, void *userdata) 
{
    if (status == WGPURequestDeviceStatus_Success) 
    {
        engineInstance.device = device;
    }
    else
    {
        AstralDebugger_Log(message);
    }
}

void AstralCanvasWgpu_Initialize(IAllocator allocator, AstralCanvas::Window* window, Array<AstralCanvas_GraphicsFeatures> requiredFeatures, Array<AstralCanvas_GraphicsFeatures> optionalFeatures)
{
    IAllocator cAllocator = GetCAllocator();

#if DEBUG
    AstralCanvasWgpu_InitLogging(WGPULogLevel_Error);
#endif

    engineInstance.instance = wgpuCreateInstance(NULL);
    if (engineInstance.instance == NULL)
    {
        THROW_ERR("Failed to initialize WGPU instance");
    }

#ifdef MACOS
    {
        CAMetalLayer metalLayer = AstralCanvas::WindowGetMetalLayer(window);

        WGPUChainedStruct fromMetalLayerType;
        fromMetalLayerType.next = NULL;
        fromMetalLayerType.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;

        WGPUSurfaceDescriptorFromMetalLayer fromMetalLayer;
        fromMetalLayer.chain = fromMetalLayerType;
        fromMetalLayer.layer = metalLayer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = (const WGPUChainedStruct*)&fromMetalLayer;
        surfaceDescriptor.label = NULL;

        engineInstance.surface = wgpuInstanceCreateSurface(engineInstance.instance, &surfaceDescriptor);
    }
#endif
#ifdef LINUX
    {
        Display *x11_display = glfwGetX11Display();
        Window x11_window = glfwGetX11Window(window->handle);

        WGPUChainedStruct fromXlibWindowType;
        fromXlibWindowType.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;

        WGPUSurfaceDescriptorFromXlibWindow fromXlibWindow;
        fromXlibWindow.chain = fromXlibWindowType;
        fromXlibWindow.display = x11_display;
        fromXlibWindow.window = x11_window;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = (const WGPUChainedStruct*)&fromXlibWindow;
        surfaceDescriptor.label = NULL;

        engineInstance.surface = wgpuInstanceCreateSurface(engineInstance.instance, &surfaceDescriptor);
    }
#endif
#ifdef WINDOWS
    {
        HWND hwnd = glfwGetWin32Window(window->handle);
        if (hwnd == NULL)
        {
            THROW_ERR("Error retrieving window handle");
        }
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUChainedStruct fromWindowsHWNDType;
        fromWindowsHWNDType.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
        fromWindowsHWNDType.next = NULL;

        WGPUSurfaceDescriptorFromWindowsHWND fromWindowsHWND;
        fromWindowsHWND.chain = fromWindowsHWNDType;
        fromWindowsHWND.hinstance = hinstance;
        fromWindowsHWND.hwnd = hwnd;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = (const WGPUChainedStruct*)&fromWindowsHWND;
        surfaceDescriptor.label = NULL;

        engineInstance.surface = wgpuInstanceCreateSurface(engineInstance.instance, &surfaceDescriptor);
    }
#endif
    if (engineInstance.surface == NULL)
    {
        THROW_ERR("Failed to create WGPU surface with the current windowing system");
    }

    WGPURequestAdapterOptions requestAdapterOptions = {};
    requestAdapterOptions.compatibleSurface = engineInstance.surface;
    requestAdapterOptions.forceFallbackAdapter = WGPUBool(false);
    requestAdapterOptions.backendType = WGPUBackendType_Vulkan;
    requestAdapterOptions.nextInChain = NULL;

    wgpuInstanceRequestAdapter(engineInstance.instance, &requestAdapterOptions, &AstralCanvasWgpu_HandleRequestedAdapter, NULL);
    if (engineInstance.adapter == NULL)
    {
        THROW_ERR("Failed to request adapter for target graphics backend");
    }

    i32 supportedRequiredFeatures = 0;
    vector<WGPUFeatureName> enabledWGPUFeatures = vector<WGPUFeatureName>(cAllocator);
    engineInstance.enabledFeatures = vector<AstralCanvas_GraphicsFeatures>(allocator);

    WGPUFeatureName allSupportedFeatures[64];
    usize featureCount = wgpuAdapterEnumerateFeatures(engineInstance.adapter, allSupportedFeatures);

    for (usize i = 0; i < featureCount; i++)
    {
        bool found = false;
        for (usize j = 0; j < requiredFeatures.length; j++)
        {
            if ((allSupportedFeatures[i] == WGPUNativeFeature_SampledTextureAndStorageBufferArrayNonUniformIndexing
            && requiredFeatures.data[j] == AstralCanvas_Feature_ArrayDynamicIndexing) ||
            (allSupportedFeatures[i] == WGPUNativeFeature_TextureBindingArray
            && requiredFeatures.data[j] == AstralCanvas_Feature_TextureArrays) ||
            (allSupportedFeatures[i] == WGPUNativeFeature_TextureBindingArray
            && requiredFeatures.data[j] == AstralCanvas_Feature_TextureArrays))
            {
                supportedRequiredFeatures++;
                enabledWGPUFeatures.Add(allSupportedFeatures[i]);
                engineInstance.enabledFeatures.Add(requiredFeatures.data[i]);
                found = true;
                break;
            }
        }
        if (!found)
        {
            for (usize j = 0; j < optionalFeatures.length; j++)
            {
                if ((allSupportedFeatures[i] == WGPUNativeFeature_SampledTextureAndStorageBufferArrayNonUniformIndexing
                && optionalFeatures.data[j] == AstralCanvas_Feature_ArrayDynamicIndexing) ||
                (allSupportedFeatures[i] == WGPUNativeFeature_TextureBindingArray
                && optionalFeatures.data[j] == AstralCanvas_Feature_TextureArrays) ||
                (allSupportedFeatures[i] == WGPUNativeFeature_TextureBindingArray
                && optionalFeatures.data[j] == AstralCanvas_Feature_TextureArrays))
                {
                    enabledWGPUFeatures.Add(allSupportedFeatures[i]);
                    engineInstance.enabledFeatures.Add(optionalFeatures.data[i]);
                    break;
                }
            }
        }
    }
    if (supportedRequiredFeatures != requiredFeatures.length)
    {
        THROW_ERR("Not all required extensions are supported");
    }

    WGPUDeviceDescriptor deviceDescriptor = {};
    deviceDescriptor.requiredFeatureCount = enabledWGPUFeatures.count;
    deviceDescriptor.requiredFeatures = enabledWGPUFeatures.ptr;
    deviceDescriptor.nextInChain = NULL;
    wgpuAdapterRequestDevice(engineInstance.adapter, &deviceDescriptor, &AstralCanvasWgpu_HandleRequestedDevice, NULL);
    if (engineInstance.device == NULL)
    {
        THROW_ERR("Failed to request device for target graphics backend and features");
    }

    enabledWGPUFeatures.deinit();

    wgpuSurfaceGetCapabilities(engineInstance.surface, engineInstance.adapter, &engineInstance.surfaceCapabilities);

    //why cant we have multiple queues in WGPU? idk
    engineInstance.queue = wgpuDeviceGetQueue(engineInstance.device);
    if (engineInstance.queue == NULL)
    {
        THROW_ERR("Failed to get queue from device");
    }

    //'resize' it so we can create the surface
    AstralCanvasWgpu_ResizeWindow(window);
}

void AstralCanvasWgpu_ResizeWindow(AstralCanvas::Window *window)
{
    engineInstance.config.device = engineInstance.device;
    engineInstance.config.usage = WGPUTextureUsage_RenderAttachment;
    //to update
    engineInstance.config.format = engineInstance.surfaceCapabilities.formats[0];
    engineInstance.config.alphaMode = engineInstance.surfaceCapabilities.alphaModes[0];
    engineInstance.config.presentMode = WGPUPresentMode_Fifo;
    engineInstance.config.nextInChain = NULL;

    i32 w;
    i32 h;
    glfwGetWindowSize(window->handle, &w, &h);

    engineInstance.config.width = (u32)w;
    engineInstance.config.height = (u32)h;

    wgpuSurfaceConfigure(engineInstance.surface, &engineInstance.config);
}

void AstralCanvasWgpu_Deinit()
{
    wgpuSurfaceCapabilitiesFreeMembers(engineInstance.surfaceCapabilities);
    if (engineInstance.queue != NULL)
        wgpuQueueRelease(engineInstance.queue);
    if (engineInstance.device != NULL)
        wgpuDeviceRelease(engineInstance.device);
    if (engineInstance.adapter != NULL)
        wgpuAdapterRelease(engineInstance.adapter);
    if (engineInstance.surface != NULL)
        wgpuSurfaceRelease(engineInstance.surface);
    if (engineInstance.instance != NULL)
        wgpuInstanceRelease(engineInstance.instance);
    printf("Deinit'd WGPU\n");
}

i32 AstralCanvasWgpu_CreateShaderFromString(IAllocator allocator, ShaderType shaderType, string jsonString, Shader *result)
{
    *result = AstralCanvas::Shader(allocator, shaderType);
    ArenaAllocator localArena = ArenaAllocator(allocator);

    JsonElement root;
    usize parseJsonResult = ParseJsonDocument(&localArena.AsAllocator(), jsonString, &root);
    if (parseJsonResult != 0)
    {
        localArena.deinit();
        return (i32)parseJsonResult;
    }

    JsonElement *computeElement = root.GetProperty("compute");

    if (computeElement != NULL)
    {

    }
    else
    {
        JsonElement *vertexElement = root.GetProperty("vertex");
        JsonElement *fragmentElement = root.GetProperty("fragment");

        if (vertexElement != NULL && fragmentElement != NULL)
        {
            JsonElement *vertexSpirv = vertexElement->GetProperty("spirv");
            JsonElement *fragmentSpirv = fragmentElement->GetProperty("spirv");

            collections::Array<u32> vertexSpirvData = collections::Array<u32>(&localArena.AsAllocator(), vertexSpirv->arrayElements.length);
            collections::Array<u32> fragmentSpirvData = collections::Array<u32>(&localArena.AsAllocator(), fragmentSpirv->arrayElements.length);

            for (usize i = 0; i < vertexSpirv->arrayElements.length; i++)
            {
                vertexSpirvData.data[i] = vertexSpirv->arrayElements.data[i].GetUint32();
            }
            for (usize i = 0; i < fragmentSpirv->arrayElements.length; i++)
            {
                fragmentSpirvData.data[i] = fragmentSpirv->arrayElements.data[i].GetUint32();
            }
            WGPUDevice device = AstralCanvasWgpu_GetEngineInstance()->device;

            WGPUChainedStruct spirvDescriptorType;
            spirvDescriptorType.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
            spirvDescriptorType.next = NULL;

            WGPUShaderModuleSPIRVDescriptor vertexSpirvDescriptor;
            vertexSpirvDescriptor.codeSize = (u32)vertexSpirvData.length;
            vertexSpirvDescriptor.code = vertexSpirvData.data;
            vertexSpirvDescriptor.chain = spirvDescriptorType;

            WGPUShaderModuleDescriptor vertexDescriptor;
            vertexDescriptor.label = "vertex";
            vertexDescriptor.hintCount = 0;
            vertexDescriptor.nextInChain = (WGPUChainedStruct *)&vertexSpirvDescriptor;

            result->shaderModule1 = wgpuDeviceCreateShaderModule(device, &vertexDescriptor);

            WGPUShaderModuleSPIRVDescriptor fragmentSpirvDescriptor;
            fragmentSpirvDescriptor.codeSize = (u32)fragmentSpirvData.length;
            fragmentSpirvDescriptor.code = fragmentSpirvData.data;
            fragmentSpirvDescriptor.chain = spirvDescriptorType;

            WGPUShaderModuleDescriptor fragmentDescriptor;
            fragmentDescriptor.label = "fragment";
            fragmentDescriptor.hintCount = 0;
            fragmentDescriptor.nextInChain = (WGPUChainedStruct *)&fragmentSpirvDescriptor;

            result->shaderModule2 = wgpuDeviceCreateShaderModule(device, &fragmentDescriptor);
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