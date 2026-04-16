/*
 * FastScreen - DXGI Desktop Duplication Implementation
 * 
 * Hardware-accelerated screen capture using DirectX
 */

#include "fastscreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Embedded HLSL Shaders for hardware scaling
// Compile with: fxc /T vs_4_0 /E VSMain
const char* g_vertexShaderCode = R"(
struct VSInput {
    float2 pos : POSITION;
    float2 tex : TEXCOORD;
};
struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};
PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.tex = input.tex;
    return output;
}
)";

// Pixel shader: Sample texture with filter, convert BGRA->RGBA
// Compile with: fxc /T ps_4_0 /E PSMain
const char* g_pixelShaderCode = R"(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);
struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};
float4 PSMain(PSInput input) : SV_TARGET {
    float4 color = g_texture.Sample(g_sampler, input.tex);
    // BGRA to RGBA swizzle
    return float4(color.b, color.g, color.r, color.a);
}
)";

class DXGICapture {
private:
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    
    // Hardware scaling resources
    ID3D11Texture2D* sourceTexture = nullptr;      // Full resolution desktop
    ID3D11Texture2D* scaledTexture = nullptr;      // Hardware-scaled output (GPU render target)
    ID3D11Texture2D* readbackTexture = nullptr;    // CPU-readable staging for scaled output
    ID3D11RenderTargetView* rtv = nullptr;         // Render target for scaling
    ID3D11ShaderResourceView* srv = nullptr;       // Source view
    ID3D11SamplerState* sampler = nullptr;       // Point or Linear filter
    ID3D11BlendState* blendState = nullptr;        // No blending needed
    
    // Shader objects
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11RasterizerState* rasterState = nullptr;
    
    int outputIndex = 0;
    int width = 0;          // Monitor full width
    int height = 0;         // Monitor full height
    int* pixelBuffer = nullptr;
    int bufferSize = 0;
    
    // Capture region (for partial screen capture)
    int captureX = 0;
    int captureY = 0;
    int captureWidth = 0;   // 0 = full screen
    int captureHeight = 0;  // 0 = full screen
    
    // Output scaling (hardware accelerated)
    int outputWidth = 0;    // Final output width (e.g., 640)
    int outputHeight = 0;   // Final output height (e.g., 480)
    bool useScaling = false;
    int scaleFilter = 0;    // 0=Point (fast), 1=Linear (smooth)
    
    // Frame pooling - eliminate malloc/free per frame
    static const int POOL_SIZE = 3;
    int* bufferPool[POOL_SIZE] = {nullptr, nullptr, nullptr};
    int poolIndex = 0;
    bool poolInitialized = false;
    
    bool createStagingTexture() {
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }
        
        // Use capture region size, not full monitor size
        int texWidth = (captureWidth > 0) ? captureWidth : width;
        int texHeight = (captureHeight > 0) ? captureHeight : height;
        
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = texWidth;
        desc.Height = texHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create staging texture (%dx%d): 0x%08X\n", texWidth, texHeight, hr);
            return false;
        }
        
        return true;
    }
    
public:
    // Setup hardware scaling with full D3D11 rendering
    bool setupHardwareScaling(int outW, int outH, int filter) {
        if (!device || !context) return false;
        
        // Cleanup existing scaling resources (inline to avoid visibility issues)
        if (rasterState) { rasterState->Release(); rasterState = nullptr; }
        if (vertexBuffer) { vertexBuffer->Release(); vertexBuffer = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
        if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (srv) { srv->Release(); srv = nullptr; }
        if (readbackTexture) { readbackTexture->Release(); readbackTexture = nullptr; }
        if (scaledTexture) { scaledTexture->Release(); scaledTexture = nullptr; }
        if (sourceTexture) { sourceTexture->Release(); sourceTexture = nullptr; }
        useScaling = false;
        
        outputWidth = outW;
        outputHeight = outH;
        useScaling = (outW > 0 && outH > 0 && (outW != captureWidth || outH != captureHeight));
        scaleFilter = filter;
        
        if (!useScaling) {
            printf("[DXGICapture] Scaling not needed or dimensions match\n");
            return true;
        }
        
        printf("[DXGICapture] Setting up HARDWARE rendering: %dx%d -> %dx%d (filter: %s)\n",
               captureWidth, captureHeight, outW, outH, filter == 0 ? "Point" : "Linear");
        
        HRESULT hr;
        
        // Compile and create vertex shader
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        hr = D3DCompile(g_vertexShaderCode, strlen(g_vertexShaderCode), "VS", nullptr, nullptr, 
                        "VSMain", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                printf("[DXGICapture] VS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return false;
        }
        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create VS: 0x%08X\n", hr);
            vsBlob->Release();
            return false;
        }
        
        // Create input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        hr = device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
        vsBlob->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create input layout: 0x%08X\n", hr);
            return false;
        }
        
        // Compile and create pixel shader
        ID3DBlob* psBlob = nullptr;
        hr = D3DCompile(g_pixelShaderCode, strlen(g_pixelShaderCode), "PS", nullptr, nullptr,
                        "PSMain", "ps_4_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                printf("[DXGICapture] PS compile error: %s\n", (char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return false;
        }
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
        psBlob->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create PS: 0x%08X\n", hr);
            return false;
        }
        
        // Create fullscreen quad vertex buffer (2 triangles)
        struct Vertex { float x, y, u, v; };
        Vertex vertices[] = {
            { -1.0f,  1.0f, 0.0f, 0.0f },  // Top-left
            {  1.0f,  1.0f, 1.0f, 0.0f },  // Top-right
            { -1.0f, -1.0f, 0.0f, 1.0f },  // Bottom-left
            {  1.0f, -1.0f, 1.0f, 1.0f }   // Bottom-right
        };
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.ByteWidth = sizeof(vertices);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
        hr = device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create vertex buffer: 0x%08X\n", hr);
            return false;
        }
        
        // Create render target texture (GPU-only, bind as render target)
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width = outW;
        rtDesc.Height = outH;
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = device->CreateTexture2D(&rtDesc, nullptr, &scaledTexture);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create render target: 0x%08X\n", hr);
            return false;
        }
        
        // Create render target view
        hr = device->CreateRenderTargetView(scaledTexture, nullptr, &rtv);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create RTV: 0x%08X\n", hr);
            return false;
        }
        
        // Create readback texture (staging for CPU)
        D3D11_TEXTURE2D_DESC rbDesc = {};
        rbDesc.Width = outW;
        rbDesc.Height = outH;
        rbDesc.MipLevels = 1;
        rbDesc.ArraySize = 1;
        rbDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rbDesc.SampleDesc.Count = 1;
        rbDesc.Usage = D3D11_USAGE_STAGING;
        rbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&rbDesc, nullptr, &readbackTexture);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create readback texture: 0x%08X\n", hr);
            return false;
        }
        
        // Create sampler
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = (filter == 0) ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sampDesc, &sampler);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create sampler: 0x%08X\n", hr);
            return false;
        }
        
        // Create rasterizer state (no culling, fill mode)
        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        hr = device->CreateRasterizerState(&rasterDesc, &rasterState);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create raster state: 0x%08X\n", hr);
            return false;
        }
        
        // Resize buffer pool for scaled output
        int newBufferSize = outW * outH;
        if (bufferSize != newBufferSize) {
            // Cleanup old pool
            if (poolInitialized) {
                for (int i = 0; i < POOL_SIZE; i++) {
                    if (bufferPool[i]) { free(bufferPool[i]); bufferPool[i] = nullptr; }
                }
            }
            // Create new pool with scaled size
            bufferSize = newBufferSize;
            for (int i = 0; i < POOL_SIZE; i++) {
                bufferPool[i] = (int*)malloc(bufferSize * sizeof(int));
                if (!bufferPool[i]) {
                    printf("[DXGICapture] Failed to allocate scaled pool buffer %d\n", i);
                    return false;
                }
            }
            poolInitialized = true;
            poolIndex = 0;
            printf("[DXGICapture] Resized frame pool for %dx%d output\n", outW, outH);
        }
        
        printf("[DXGICapture] HARDWARE rendering setup complete!\n");
        return true;
    }
    
public:
    DXGICapture() {}
    
    ~DXGICapture() {
        cleanup();
    }
    
    bool initialize(int monitorIndex = 0, int x = 0, int y = 0, int w = 0, int h = 0) {
        printf("[DXGICapture] Initializing for monitor %d region (%d,%d %dx%d)\n", 
               monitorIndex, x, y, w, h);
        
        HRESULT hr;
        
        // Store capture region
        captureX = x;
        captureY = y;
        captureWidth = w;
        captureHeight = h;
        
        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL obtainedLevel;
        
        hr = D3D11CreateDevice(
            nullptr,                    // Adapter (nullptr = default)
            D3D_DRIVER_TYPE_HARDWARE,   // Driver type
            nullptr,                    // Software
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,  // Flags
            featureLevels,              // Feature levels
            2,                          // Num feature levels
            D3D11_SDK_VERSION,          // SDK version
            &device,                    // Device
            &obtainedLevel,             // Obtained level
            &context                    // Context
        );
        
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create D3D11 device: 0x%08X\n", hr);
            return false;
        }
        
        // Get DXGI device
        IDXGIDevice* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to get DXGI device: 0x%08X\n", hr);
            return false;
        }
        
        // Get adapter
        IDXGIAdapter* dxgiAdapter = nullptr;
        hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
        dxgiDevice->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to get DXGI adapter: 0x%08X\n", hr);
            return false;
        }
        
        // Get output (monitor)
        IDXGIOutput* dxgiOutput = nullptr;
        hr = dxgiAdapter->EnumOutputs(monitorIndex, &dxgiOutput);
        dxgiAdapter->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to enumerate output %d: 0x%08X\n", monitorIndex, hr);
            return false;
        }
        
        // Get output description for dimensions
        DXGI_OUTPUT_DESC outputDesc;
        hr = dxgiOutput->GetDesc(&outputDesc);
        if (SUCCEEDED(hr)) {
            width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
            height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
            printf("[DXGICapture] Monitor %d: %dx%d\n", monitorIndex, width, height);
        }
        
        // Validate capture region
        if (captureWidth <= 0 || captureWidth > width) captureWidth = width;
        if (captureHeight <= 0 || captureHeight > height) captureHeight = height;
        if (captureX < 0) captureX = 0;
        if (captureY < 0) captureY = 0;
        if (captureX + captureWidth > width) captureWidth = width - captureX;
        if (captureY + captureHeight > height) captureHeight = height - captureY;
        
        printf("[DXGICapture] Capture region: (%d,%d %dx%d)\n", captureX, captureY, captureWidth, captureHeight);
        
        // Create desktop duplication
        IDXGIOutput1* dxgiOutput1 = nullptr;
        hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
        dxgiOutput->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to get IDXGIOutput1: 0x%08X\n", hr);
            return false;
        }
        
        hr = dxgiOutput1->DuplicateOutput(device, &duplication);
        dxgiOutput1->Release();
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
                printf("[DXGICapture] Desktop Duplication already in use by another application\n");
            } else if (hr == E_INVALIDARG || hr == 0x80070057) {
                printf("[DXGICapture] Invalid parameter - virtual desktop or unsupported format\n");
            } else {
                printf("[DXGICapture] Failed to create duplication: 0x%08X\n", hr);
            }
            return false;
        }
        
        // Create staging texture for capture region size
        if (!createStagingTexture()) {
            return false;
        }
        
        // Initialize frame pool
        bufferSize = captureWidth * captureHeight;
        if (!poolInitialized) {
            for (int i = 0; i < POOL_SIZE; i++) {
                bufferPool[i] = (int*)malloc(bufferSize * sizeof(int));
                if (!bufferPool[i]) {
                    printf("[DXGICapture] Failed to allocate pool buffer %d\n", i);
                    return false;
                }
            }
            poolInitialized = true;
            poolIndex = 0;
            printf("[DXGICapture] Frame pool initialized (%d buffers x %d pixels)\n", POOL_SIZE, bufferSize);
        }
        
        // Set initial pixel buffer to first pool slot
        pixelBuffer = bufferPool[0];
        
        outputIndex = monitorIndex;
        printf("[DXGICapture] Initialization complete\n");
        return true;
    }
    
    bool captureFrame(int** pixels, int* outWidth, int* outHeight) {
        if (!duplication || !device || !context) {
            return false;
        }
        
        IDXGIResource* desktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        // Try to acquire next frame (100ms timeout)
        HRESULT hr = duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // No new frame
        }
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to acquire frame: 0x%08X\n", hr);
            return false;
        }
        
        // Get texture from resource
        ID3D11Texture2D* desktopTexture = nullptr;
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopTexture);
        desktopResource->Release();
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return false;
        }
        
        // HARDWARE SCALING PATH: Use GPU rendering
        if (useScaling && vertexShader && pixelShader && scaledTexture) {
            // Create SRV for desktop texture if not exists
            if (!srv) {
                hr = device->CreateShaderResourceView(desktopTexture, nullptr, &srv);
                if (FAILED(hr)) {
                    printf("[DXGICapture] Failed to create SRV: 0x%08X\n", hr);
                    desktopTexture->Release();
                    duplication->ReleaseFrame();
                    return false;
                }
            } else {
                // Update SRV to point to new desktop texture
                srv->Release();
                hr = device->CreateShaderResourceView(desktopTexture, nullptr, &srv);
                if (FAILED(hr)) {
                    printf("[DXGICapture] Failed to update SRV: 0x%08X\n", hr);
                    desktopTexture->Release();
                    duplication->ReleaseFrame();
                    return false;
                }
            }
            
            // Set render target
            context->OMSetRenderTargets(1, &rtv, nullptr);
            
            // Set viewport
            D3D11_VIEWPORT viewport = {};
            viewport.Width = (float)outputWidth;
            viewport.Height = (float)outputHeight;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            
            // Set shaders
            context->VSSetShader(vertexShader, nullptr, 0);
            context->PSSetShader(pixelShader, nullptr, 0);
            context->PSSetSamplers(0, 1, &sampler);
            context->PSSetShaderResources(0, 1, &srv);
            
            // Set input layout and vertex buffer
            context->IASetInputLayout(inputLayout);
            UINT stride = 16; // 4 floats * 4 bytes
            UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            
            // Set rasterizer state
            context->RSSetState(rasterState);
            
            // Draw fullscreen quad (4 vertices = 2 triangles)
            context->Draw(4, 0);
            
            // Copy from render target to readback texture
            context->CopyResource(readbackTexture, scaledTexture);
            
            // Map readback texture
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = context->Map(readbackTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
            if (FAILED(hr)) {
                printf("[DXGICapture] Failed to map readback: 0x%08X\n", hr);
                desktopTexture->Release();
                duplication->ReleaseFrame();
                return false;
            }
            
            // Get next buffer from pool
            pixelBuffer = bufferPool[poolIndex];
            poolIndex = (poolIndex + 1) % POOL_SIZE;
            
            // Copy pixels (already RGBA from shader!)
            BYTE* srcPixels = (BYTE*)mappedResource.pData;
            for (int y = 0; y < outputHeight; y++) {
                memcpy(&pixelBuffer[y * outputWidth], 
                       &srcPixels[y * mappedResource.RowPitch], 
                       outputWidth * 4);
            }
            
            context->Unmap(readbackTexture, 0);
            desktopTexture->Release();
            duplication->ReleaseFrame();
            
            *pixels = pixelBuffer;
            *outWidth = outputWidth;
            *outHeight = outputHeight;
            
            return true;
        }
        
        // FALLBACK PATH: Original CPU-based conversion (no scaling)
        // Copy to staging texture
        context->CopyResource(stagingTexture, desktopTexture);
        desktopTexture->Release();
        
        // Map staging texture for CPU read
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return false;
        }
        
        // Get next buffer from pool (round-robin)
        pixelBuffer = bufferPool[poolIndex];
        poolIndex = (poolIndex + 1) % POOL_SIZE;
        
        // Convert BGRA to RGBA - use capture region dimensions
        int outW = (captureWidth > 0) ? captureWidth : width;
        int outH = (captureHeight > 0) ? captureHeight : height;
        
        BYTE* srcPixels = (BYTE*)mappedResource.pData;
        for (int y = 0; y < outH; y++) {
            for (int x = 0; x < outW; x++) {
                int srcIdx = y * mappedResource.RowPitch + x * 4;
                int dstIdx = y * outW + x;
                
                BYTE b = srcPixels[srcIdx + 0];
                BYTE g = srcPixels[srcIdx + 1];
                BYTE r = srcPixels[srcIdx + 2];
                BYTE a = srcPixels[srcIdx + 3];
                
                pixelBuffer[dstIdx] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        
        context->Unmap(stagingTexture, 0);
        duplication->ReleaseFrame();
        
        *pixels = pixelBuffer;
        *outWidth = outW;
        *outHeight = outH;
        
        return true;
    }
    
    void cleanup() {
        // Free all pooled buffers
        if (poolInitialized) {
            for (int i = 0; i < POOL_SIZE; i++) {
                if (bufferPool[i]) {
                    free(bufferPool[i]);
                    bufferPool[i] = nullptr;
                }
            }
            poolInitialized = false;
            poolIndex = 0;
        }
        pixelBuffer = nullptr;
        
        // Release hardware scaling resources
        if (rasterState) { rasterState->Release(); rasterState = nullptr; }
        if (vertexBuffer) { vertexBuffer->Release(); vertexBuffer = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
        if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
        if (blendState) { blendState->Release(); blendState = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (srv) { srv->Release(); srv = nullptr; }
        if (readbackTexture) { readbackTexture->Release(); readbackTexture = nullptr; }
        if (scaledTexture) { scaledTexture->Release(); scaledTexture = nullptr; }
        if (sourceTexture) { sourceTexture->Release(); sourceTexture = nullptr; }
        
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }
        if (duplication) {
            duplication->Release();
            duplication = nullptr;
        }
        if (context) {
            context->Release();
            context = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
        useScaling = false;
        width = 0;
        height = 0;
        bufferSize = 0;
    }
    
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

// C interface for JNI
extern "C" {

    void* dxgiCreateCapture() {
        return new DXGICapture();
    }
    
    // Legacy initialize (full screen)
    bool dxgiInitialize(void* capture, int monitorIndex) {
        return static_cast<DXGICapture*>(capture)->initialize(monitorIndex, 0, 0, 0, 0);
    }
    
    // Region-based initialize
    bool dxgiInitializeRegion(void* capture, int monitorIndex, int x, int y, int w, int h) {
        return static_cast<DXGICapture*>(capture)->initialize(monitorIndex, x, y, w, h);
    }
    
    // Setup hardware scaling (output size and filter: 0=Point, 1=Linear)
    bool dxgiSetupScaling(void* capture, int outW, int outH, int filter) {
        return static_cast<DXGICapture*>(capture)->setupHardwareScaling(outW, outH, filter);
    }
    
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height) {
        return static_cast<DXGICapture*>(capture)->captureFrame(pixels, width, height);
    }
    
    void dxgiDestroyCapture(void* capture) {
        delete static_cast<DXGICapture*>(capture);
    }
    
}
