/**
 * @file DXGICapture.cpp
 * @brief DXGI Desktop Duplication implementation for hardware-accelerated capture
 * 
 * @details Implements DirectX 11 based screen capture using DXGI Desktop Duplication
 * API. Provides GPU-accelerated texture capture, hardware scaling via shaders,
 * 64-byte aligned triple-buffered frame pooling, and subresource region cropping.
 * 
 * @par Architecture
 * - D3D11 device creation with BGRA support
 * - DXGI Output Duplication for frame acquisition
 * - Staging texture for CPU readback (with dynamic region resizing)
 * - CopySubresourceRegion for zero-overhead region capture
 * - Hardware scaling with cached vertex/pixel shaders and SRV
 * - 64-byte AVX2/AVX-512 aligned triple-buffered frame pool
 * - Automatic recovery on DXGI_ERROR_ACCESS_LOST
 * 
 * @author FastJava Team
 * @version 0.1.3
 * @copyright MIT License
 */

#include "fastscreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Embedded HLSL Shaders for hardware scaling
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
const char* g_pixelShaderCode = R"(
Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);
struct PSInput {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};
float4 PSMain(PSInput input) : SV_TARGET {
    float4 color = g_texture.Sample(g_sampler, input.tex);
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
    ID3D11ShaderResourceView* srv = nullptr;       // Source view (cached)
    ID3D11Resource* lastSrvResource = nullptr;     // Pointer comparison to cache SRV
    ID3D11SamplerState* sampler = nullptr;         // Point or Linear filter
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
    
    // Frame pooling - 64-byte AVX2/AVX-512 aligned memory
    static const int POOL_SIZE = 3;
    int* bufferPool[POOL_SIZE] = {nullptr, nullptr, nullptr};
    int poolIndex = 0;
    bool poolInitialized = false;

    // High-speed Win32 GDI DIBSection fallback when DXGI is unavailable
    bool useGdiFallback = false;
    HDC hdcScreen = nullptr;
    HDC hdcMem = nullptr;
    HBITMAP hBitmap = nullptr;
    void* gdiPixels = nullptr;

    void freeBufferPool() {
        if (poolInitialized) {
            for (int i = 0; i < POOL_SIZE; i++) {
                if (bufferPool[i]) {
                    _aligned_free(bufferPool[i]);
                    bufferPool[i] = nullptr;
                }
            }
            poolInitialized = false;
            poolIndex = 0;
        }
        pixelBuffer = nullptr;
    }

    bool allocateBufferPool(int totalPixels) {
        if (poolInitialized && bufferSize == totalPixels) {
            return true;
        }
        freeBufferPool();

        bufferSize = totalPixels;
        for (int i = 0; i < POOL_SIZE; i++) {
            // 64-byte alignment for AVX2 and AVX-512 cache lines
            bufferPool[i] = (int*)_aligned_malloc(bufferSize * sizeof(int), 64);
            if (!bufferPool[i]) {
                printf("[DXGICapture] Failed to allocate aligned pool buffer %d\n", i);
                freeBufferPool();
                return false;
            }
        }
        poolInitialized = true;
        poolIndex = 0;
        pixelBuffer = bufferPool[0];
        return true;
    }
    
    bool createStagingTexture() {
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }
        
        int texWidth = (captureWidth > 0) ? captureWidth : width;
        int texHeight = (captureHeight > 0) ? captureHeight : height;

        if (useGdiFallback) {
            if (hBitmap) { DeleteObject(hBitmap); hBitmap = nullptr; }
            if (hdcMem) { DeleteDC(hdcMem); hdcMem = nullptr; }
            if (hdcScreen) { ReleaseDC(NULL, hdcScreen); hdcScreen = nullptr; }

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = texWidth;
            bmi.bmiHeader.biHeight = -texHeight; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            
            hdcScreen = GetDC(NULL);
            hdcMem = CreateCompatibleDC(hdcScreen);
            hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, (void**)&gdiPixels, NULL, 0);
            if (!hBitmap || !gdiPixels) {
                printf("[DXGICapture] Failed to create GDI DIBSection (%dx%d)\n", texWidth, texHeight);
                return false;
            }
            SelectObject(hdcMem, hBitmap);
            return true;
        }
        
        if (!device) return false;

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

    bool recreateDuplication() {
        if (duplication) {
            duplication->Release();
            duplication = nullptr;
        }
        if (!device) return false;

        IDXGIDevice* dxgiDevice = nullptr;
        HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) return false;

        IDXGIAdapter* dxgiAdapter = nullptr;
        hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
        dxgiDevice->Release();
        if (FAILED(hr)) return false;

        IDXGIOutput* dxgiOutput = nullptr;
        hr = dxgiAdapter->EnumOutputs(outputIndex, &dxgiOutput);
        dxgiAdapter->Release();
        if (FAILED(hr)) return false;

        // Query new resolution in case access lost was caused by display mode change
        DXGI_OUTPUT_DESC outputDesc;
        hr = dxgiOutput->GetDesc(&outputDesc);
        if (SUCCEEDED(hr)) {
            width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
            height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
        }

        IDXGIOutput1* dxgiOutput1 = nullptr;
        hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
        dxgiOutput->Release();
        if (FAILED(hr)) return false;

        hr = dxgiOutput1->DuplicateOutput(device, &duplication);
        dxgiOutput1->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to recreate Desktop Duplication: 0x%08X\n", hr);
            return false;
        }

        // Recreate staging texture with new dimensions if needed
        createStagingTexture();

        printf("[DXGICapture] Desktop Duplication recovered successfully (%dx%d)!\n", width, height);
        return true;
    }

public:
    DXGICapture() {}
    
    ~DXGICapture() {
        cleanup();
    }

    // Dynamic region update without destroying the D3D11 device or duplication session
    bool setRegion(int x, int y, int w, int h) {
        if (w <= 0 || w > width) w = width;
        if (h <= 0 || h > height) h = height;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > width) w = width - x;
        if (y + h > height) h = height - y;

        bool sizeChanged = (w != captureWidth || h != captureHeight);

        captureX = x;
        captureY = y;
        captureWidth = w;
        captureHeight = h;

        if (sizeChanged && !useScaling) {
            if (!createStagingTexture()) return false;
            if (!allocateBufferPool(w * h)) return false;
        }

        // If scaling is active, re-create the vertex buffer with updated subregion UV coordinates
        if (useScaling && device) {
            float u0 = (width > 0) ? (float)captureX / (float)width : 0.0f;
            float v0 = (height > 0) ? (float)captureY / (float)height : 0.0f;
            float u1 = (width > 0) ? (float)(captureX + captureWidth) / (float)width : 1.0f;
            float v1 = (height > 0) ? (float)(captureY + captureHeight) / (float)height : 1.0f;

            struct Vertex { float x, y, u, v; };
            Vertex vertices[] = {
                { -1.0f,  1.0f, u0, v0 },  // Top-left
                {  1.0f,  1.0f, u1, v0 },  // Top-right
                { -1.0f, -1.0f, u0, v1 },  // Bottom-left
                {  1.0f, -1.0f, u1, v1 }   // Bottom-right
            };

            if (vertexBuffer) {
                vertexBuffer->Release();
                vertexBuffer = nullptr;
            }

            D3D11_BUFFER_DESC vbDesc = {};
            vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vbDesc.ByteWidth = sizeof(vertices);
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
            device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer);
        }

        return true;
    }

    // Setup hardware scaling with full D3D11 rendering
    bool setupHardwareScaling(int outW, int outH, int filter) {
        if (!device || !context) return false;
        
        // Cleanup existing scaling resources
        if (rasterState) { rasterState->Release(); rasterState = nullptr; }
        if (vertexBuffer) { vertexBuffer->Release(); vertexBuffer = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
        if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (srv) { srv->Release(); srv = nullptr; lastSrvResource = nullptr; }
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
            // Re-ensure pool matches raw region
            int rawW = (captureWidth > 0) ? captureWidth : width;
            int rawH = (captureHeight > 0) ? captureHeight : height;
            allocateBufferPool(rawW * rawH);
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
        
        // Compute UV coordinates according to capture region
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;

        if (width > 0 && height > 0 && captureWidth > 0 && captureHeight > 0) {
            u0 = (float)captureX / (float)width;
            v0 = (float)captureY / (float)height;
            u1 = (float)(captureX + captureWidth) / (float)width;
            v1 = (float)(captureY + captureHeight) / (float)height;
        }

        // Fullscreen quad with region-mapped UVs
        struct Vertex { float x, y, u, v; };
        Vertex vertices[] = {
            { -1.0f,  1.0f, u0, v0 },  // Top-left
            {  1.0f,  1.0f, u1, v0 },  // Top-right
            { -1.0f, -1.0f, u0, v1 },  // Bottom-left
            {  1.0f, -1.0f, u1, v1 }   // Bottom-right
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
        
        // Render target texture (GPU only)
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
        
        hr = device->CreateRenderTargetView(scaledTexture, nullptr, &rtv);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create RTV: 0x%08X\n", hr);
            return false;
        }
        
        // Readback staging texture for CPU
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
        
        // Sampler
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
        
        // Rasterizer state
        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        hr = device->CreateRasterizerState(&rasterDesc, &rasterState);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create raster state: 0x%08X\n", hr);
            return false;
        }
        
        // Allocate aligned buffer pool for scaled output
        if (!allocateBufferPool(outW * outH)) {
            return false;
        }
        
        printf("[DXGICapture] HARDWARE rendering setup complete!\n");
        return true;
    }

    bool initialize(int monitorIndex = 0, int x = 0, int y = 0, int w = 0, int h = 0) {
        printf("[DXGICapture] Initializing for monitor %d region (%d,%d %dx%d)\n", 
               monitorIndex, x, y, w, h);
        
        HRESULT hr;
        outputIndex = monitorIndex;
        
        captureX = x;
        captureY = y;
        captureWidth = w;
        captureHeight = h;
        
        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
        D3D_FEATURE_LEVEL obtainedLevel;
        
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels,
            2,
            D3D11_SDK_VERSION,
            &device,
            &obtainedLevel,
            &context
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
            printf("[DXGICapture] DXGI Desktop Duplication unavailable (0x%08X). Activating Win32 GDI fallback...\n", hr);
            useGdiFallback = true;
            duplication = nullptr;
            if (context) { context->Release(); context = nullptr; }
            if (device) { device->Release(); device = nullptr; }
        }
        
        // Create staging texture
        if (!createStagingTexture()) {
            return false;
        }
        
        // Initialize 64-byte aligned frame pool
        if (!allocateBufferPool(captureWidth * captureHeight)) {
            return false;
        }
        
        printf("[DXGICapture] Initialization complete\n");
        return true;
    }
    
    bool captureFrame(int** pixels, int* outWidth, int* outHeight) {
        if (useGdiFallback) {
            int outW = (captureWidth > 0) ? captureWidth : width;
            int outH = (captureHeight > 0) ? captureHeight : height;
            
            BitBlt(hdcMem, 0, 0, outW, outH, hdcScreen, captureX, captureY, SRCCOPY | CAPTUREBLT);
            
            pixelBuffer = bufferPool[poolIndex];
            poolIndex = (poolIndex + 1) % POOL_SIZE;
            
            int total = outW * outH;
            BYTE* src = (BYTE*)gdiPixels;
            for (int i = 0; i < total; i++) {
                BYTE b = src[i * 4 + 0];
                BYTE g = src[i * 4 + 1];
                BYTE r = src[i * 4 + 2];
                BYTE a = 0xFF;
                pixelBuffer[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            
            *pixels = pixelBuffer;
            *outWidth = outW;
            *outHeight = outH;
            return true;
        }

        if (!duplication || !device || !context) {
            return false;
        }
        
        IDXGIResource* desktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        // Non-blocking acquire next frame (0ms timeout)
        HRESULT hr = duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // No new frame
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            printf("[DXGICapture] DXGI_ERROR_ACCESS_LOST detected. Attempting recovery...\n");
            if (recreateDuplication()) {
                // Retry once
                hr = duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);
                if (FAILED(hr)) return false;
            } else {
                return false;
            }
        } else if (FAILED(hr)) {
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
            // Check if SRV needs to be created or updated (only if desktopTexture changed)
            if (!srv || lastSrvResource != desktopTexture) {
                if (srv) {
                    srv->Release();
                    srv = nullptr;
                }
                hr = device->CreateShaderResourceView(desktopTexture, nullptr, &srv);
                if (FAILED(hr)) {
                    printf("[DXGICapture] Failed to create SRV: 0x%08X\n", hr);
                    desktopTexture->Release();
                    duplication->ReleaseFrame();
                    lastSrvResource = nullptr;
                    return false;
                }
                lastSrvResource = desktopTexture;
            }
            
            // Set render target & viewport
            context->OMSetRenderTargets(1, &rtv, nullptr);
            
            D3D11_VIEWPORT viewport = {};
            viewport.Width = (float)outputWidth;
            viewport.Height = (float)outputHeight;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            
            // Set shaders & pipeline
            context->VSSetShader(vertexShader, nullptr, 0);
            context->PSSetShader(pixelShader, nullptr, 0);
            context->PSSetSamplers(0, 1, &sampler);
            context->PSSetShaderResources(0, 1, &srv);
            
            context->IASetInputLayout(inputLayout);
            UINT stride = 16;
            UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->RSSetState(rasterState);
            
            // Draw fullscreen quad
            context->Draw(4, 0);
            
            // Copy render target to readback texture
            context->CopyResource(readbackTexture, scaledTexture);
            
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = context->Map(readbackTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
            if (FAILED(hr)) {
                printf("[DXGICapture] Failed to map readback: 0x%08X\n", hr);
                desktopTexture->Release();
                duplication->ReleaseFrame();
                return false;
            }
            
            pixelBuffer = bufferPool[poolIndex];
            poolIndex = (poolIndex + 1) % POOL_SIZE;
            
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
        
        // STANDARD PATH: CPU readback with CopySubresourceRegion support
        int outW = (captureWidth > 0) ? captureWidth : width;
        int outH = (captureHeight > 0) ? captureHeight : height;

        bool isSubRegion = (captureX > 0 || captureY > 0 || outW < width || outH < height);
        if (isSubRegion) {
            D3D11_BOX box;
            box.left = (UINT)captureX;
            box.top = (UINT)captureY;
            box.front = 0;
            box.right = (UINT)(captureX + outW);
            box.bottom = (UINT)(captureY + outH);
            box.back = 1;
            context->CopySubresourceRegion(stagingTexture, 0, 0, 0, 0, desktopTexture, 0, &box);
        } else {
            context->CopyResource(stagingTexture, desktopTexture);
        }
        desktopTexture->Release();
        
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return false;
        }
        
        pixelBuffer = bufferPool[poolIndex];
        poolIndex = (poolIndex + 1) % POOL_SIZE;
        
        BYTE* srcPixels = (BYTE*)mappedResource.pData;
        for (int y = 0; y < outH; y++) {
            const BYTE* rowSrc = &srcPixels[y * mappedResource.RowPitch];
            int* rowDst = &pixelBuffer[y * outW];
            for (int x = 0; x < outW; x++) {
                BYTE b = rowSrc[x * 4 + 0];
                BYTE g = rowSrc[x * 4 + 1];
                BYTE r = rowSrc[x * 4 + 2];
                BYTE a = rowSrc[x * 4 + 3];
                rowDst[x] = (a << 24) | (r << 16) | (g << 8) | b;
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
        freeBufferPool();
        
        if (rasterState) { rasterState->Release(); rasterState = nullptr; }
        if (vertexBuffer) { vertexBuffer->Release(); vertexBuffer = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
        if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
        if (blendState) { blendState->Release(); blendState = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (srv) { srv->Release(); srv = nullptr; }
        lastSrvResource = nullptr;
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
        if (hBitmap) {
            DeleteObject(hBitmap);
            hBitmap = nullptr;
        }
        if (hdcMem) {
            DeleteDC(hdcMem);
            hdcMem = nullptr;
        }
        if (hdcScreen) {
            ReleaseDC(NULL, hdcScreen);
            hdcScreen = nullptr;
        }
        gdiPixels = nullptr;
        useGdiFallback = false;
        useScaling = false;
        width = 0;
        height = 0;
        bufferSize = 0;
    }

    int getWidth() const { return (outputWidth > 0) ? outputWidth : ((captureWidth > 0) ? captureWidth : width); }
    int getHeight() const { return (outputHeight > 0) ? outputHeight : ((captureHeight > 0) ? captureHeight : height); }
};

// C interface for JNI
extern "C" {

    void* dxgiCreateCapture() {
        return new DXGICapture();
    }
    
    bool dxgiInitialize(void* capture, int monitorIndex) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->initialize(monitorIndex, 0, 0, 0, 0);
    }
    
    bool dxgiInitializeRegion(void* capture, int monitorIndex, int x, int y, int w, int h) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->initialize(monitorIndex, x, y, w, h);
    }

    bool dxgiSetRegion(void* capture, int x, int y, int w, int h) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->setRegion(x, y, w, h);
    }
    
    bool dxgiSetupScaling(void* capture, int outW, int outH, int filter) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->setupHardwareScaling(outW, outH, filter);
    }
    
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->captureFrame(pixels, width, height);
    }

    int dxgiGetWidth(void* capture) {
        if (!capture) return 0;
        return static_cast<DXGICapture*>(capture)->getWidth();
    }

    int dxgiGetHeight(void* capture) {
        if (!capture) return 0;
        return static_cast<DXGICapture*>(capture)->getHeight();
    }
    
    void dxgiDestroyCapture(void* capture) {
        if (capture) {
            delete static_cast<DXGICapture*>(capture);
        }
    }

    int dxgiQueryMonitorCount() {
        IDXGIFactory1* factory = nullptr;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
        if (FAILED(hr) || !factory) {
            return 1;
        }

        int totalOutputs = 0;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            IDXGIOutput* output = nullptr;
            for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
                totalOutputs++;
                output->Release();
            }
            adapter->Release();
        }
        factory->Release();
        return (totalOutputs > 0) ? totalOutputs : 1;
    }
}

