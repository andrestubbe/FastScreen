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
 * - Hardware GPU scaling: full-screen quad VS+PS blit, D3D11_FILTER_MIN_MAG_MIP_LINEAR
 * - 64-byte AVX2/AVX-512 aligned triple-buffered frame pool
 * - Automatic recovery on DXGI_ERROR_ACCESS_LOST
 * 
 * @author FastJava Team
 * @version 0.1.5
 * @copyright MIT License
 */

#include "fastscreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <mutex>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------------
// GPU scaling HLSL shaders (compiled at runtime, no shader file needed)
// ---------------------------------------------------------------------------
static const char* VS_HLSL = R"HLSL(
struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
VS_OUT main(uint id : SV_VertexID) {
    // Full-screen triangle trick (3 vertices, no VB needed)
    float2 uv  = float2((id << 1) & 2, id & 2);
    float4 pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    VS_OUT o;
    o.pos = pos;
    o.uv  = uv;
    return o;
}
)HLSL";

static const char* PS_HLSL = R"HLSL(
Texture2D    tex : register(t0);
SamplerState smp : register(s0);
struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(VS_OUT i) : SV_TARGET {
    return tex.Sample(smp, i.uv);
}
)HLSL";

// ---------------------------------------------------------------------------

class DXGICapture {
private:
    ID3D11Device*             device          = nullptr;
    ID3D11DeviceContext*      context         = nullptr;
    IDXGIOutputDuplication*   duplication     = nullptr;
    ID3D11Texture2D*          stagingTexture  = nullptr;

    int outputIndex = 0;
    int width  = 0;  // Monitor full width
    int height = 0;  // Monitor full height
    int* pixelBuffer = nullptr;
    int  bufferSize  = 0;

    // Capture region (for partial screen capture)
    int captureX      = 0;
    int captureY      = 0;
    int captureWidth  = 0;  // 0 = full screen
    int captureHeight = 0;  // 0 = full screen

    // GPU hardware scaling (0 = disabled)
    int              scaleWidth   = 0;
    int              scaleHeight  = 0;
    ID3D11Texture2D*          scaledTexture   = nullptr; // RenderTarget in scaleW x scaleH
    ID3D11RenderTargetView*   scaledRTV       = nullptr;
    ID3D11ShaderResourceView* desktopSRV      = nullptr; // SRV of intermediate copy
    ID3D11Texture2D*          desktopCopy     = nullptr; // SHADER_RESOURCE copy of desktop
    ID3D11VertexShader*       vertexShader    = nullptr;
    ID3D11PixelShader*        pixelShader     = nullptr;
    ID3D11SamplerState*       linearSampler   = nullptr;
    bool                      scaleReady      = false;

    // Frame pooling - 64-byte AVX2/AVX-512 aligned memory
    static const int POOL_SIZE = 3;
    int* bufferPool[POOL_SIZE] = {nullptr, nullptr, nullptr};
    int  poolIndex       = 0;
    bool poolInitialized = false;

    // High-speed Win32 GDI DIBSection fallback when DXGI is unavailable
    bool  useGdiFallback    = false;
    HDC   hdcScreen         = nullptr;
    HDC   hdcMem            = nullptr;
    HBITMAP hBitmap         = nullptr;
    void* gdiPixels         = nullptr;
    // Throttling for recovery during virtual desktop switch / UAC / lock
    ULONGLONG lastRecoveryAttempt = 0;
    bool      isAccessLost        = false;

    // -----------------------------------------------------------------------
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
        // Output size: scaling dimensions if active, else capture region
        int outW = (scaleWidth  > 0) ? scaleWidth  : ((captureWidth  > 0) ? captureWidth  : width);
        int outH = (scaleHeight > 0) ? scaleHeight : ((captureHeight > 0) ? captureHeight : height);
        int minPixels = outW * outH;
        if (minPixels <= 0) minPixels = width * height;
        int required = (totalPixels > minPixels) ? totalPixels : minPixels;

        if (poolInitialized && bufferSize >= required) {
            return true;
        }
        freeBufferPool();

        bufferSize = required;
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

    // -----------------------------------------------------------------------
    bool createStagingTexture() {
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }

        // If GPU scaling is active, staging texture has the output (scaled) dimensions
        int texWidth  = (scaleWidth  > 0) ? scaleWidth  : ((captureWidth  > 0) ? captureWidth  : width);
        int texHeight = (scaleHeight > 0) ? scaleHeight : ((captureHeight > 0) ? captureHeight : height);

        if (useGdiFallback) {
            if (hBitmap)    { DeleteObject(hBitmap);        hBitmap    = nullptr; }
            if (hdcMem)     { DeleteDC(hdcMem);             hdcMem     = nullptr; }
            if (hdcScreen)  { ReleaseDC(NULL, hdcScreen);   hdcScreen  = nullptr; }

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = texWidth;
            bmi.bmiHeader.biHeight      = -texHeight; // top-down
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            hdcScreen = GetDC(NULL);
            hdcMem    = CreateCompatibleDC(hdcScreen);
            hBitmap   = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, (void**)&gdiPixels, NULL, 0);
            if (!hBitmap || !gdiPixels) {
                printf("[DXGICapture] Failed to create GDI DIBSection (%dx%d)\n", texWidth, texHeight);
                return false;
            }
            SelectObject(hdcMem, hBitmap);
            return true;
        }

        if (!device) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width          = (UINT)texWidth;
        desc.Height         = (UINT)texHeight;
        desc.MipLevels      = 1;
        desc.ArraySize      = 1;
        desc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create staging texture (%dx%d): 0x%08X\n", texWidth, texHeight, hr);
            return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Build all GPU-scaling resources (scaledTexture RT + desktopCopy + shaders + sampler)
    bool createScaleResources() {
        freeScaleResources();

        if (!device || scaleWidth <= 0 || scaleHeight <= 0) return false;

        HRESULT hr;

        // 1. Intermediate copy of desktop texture (needed for SRV)
        //    Size = full capture region (before scaling)
        int srcW = (captureWidth  > 0) ? captureWidth  : width;
        int srcH = (captureHeight > 0) ? captureHeight : height;

        D3D11_TEXTURE2D_DESC copyDesc = {};
        copyDesc.Width          = (UINT)srcW;
        copyDesc.Height         = (UINT)srcH;
        copyDesc.MipLevels      = 1;
        copyDesc.ArraySize      = 1;
        copyDesc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
        copyDesc.SampleDesc.Count = 1;
        copyDesc.Usage          = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags      = 0;

        hr = device->CreateTexture2D(&copyDesc, nullptr, &desktopCopy);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create desktopCopy texture (%dx%d): 0x%08X\n", srcW, srcH, hr);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        hr = device->CreateShaderResourceView(desktopCopy, &srvDesc, &desktopSRV);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create desktop SRV: 0x%08X\n", hr);
            return false;
        }

        // 2. Scaled RenderTarget texture (scaleWidth x scaleHeight)
        D3D11_TEXTURE2D_DESC rtDesc = {};
        rtDesc.Width          = (UINT)scaleWidth;
        rtDesc.Height         = (UINT)scaleHeight;
        rtDesc.MipLevels      = 1;
        rtDesc.ArraySize      = 1;
        rtDesc.Format         = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage          = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags      = D3D11_BIND_RENDER_TARGET;
        rtDesc.CPUAccessFlags = 0;
        rtDesc.MiscFlags      = 0;

        hr = device->CreateTexture2D(&rtDesc, nullptr, &scaledTexture);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create scaledTexture (%dx%d): 0x%08X\n", scaleWidth, scaleHeight, hr);
            return false;
        }

        hr = device->CreateRenderTargetView(scaledTexture, nullptr, &scaledRTV);
        if (FAILED(hr)) {
            printf("[DXGICapture] Failed to create scaledRTV: 0x%08X\n", hr);
            return false;
        }

        // 3. Compile Vertex Shader
        ID3DBlob* vsBlob  = nullptr;
        ID3DBlob* errBlob = nullptr;
        hr = D3DCompile(VS_HLSL, strlen(VS_HLSL), "VS", nullptr, nullptr,
                        "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
        if (FAILED(hr)) {
            printf("[DXGICapture] VS compile failed: %s\n",
                   errBlob ? (char*)errBlob->GetBufferPointer() : "?");
            if (errBlob) errBlob->Release();
            return false;
        }
        if (errBlob) errBlob->Release();
        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        vsBlob->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] CreateVertexShader failed: 0x%08X\n", hr);
            return false;
        }

        // 4. Compile Pixel Shader
        ID3DBlob* psBlob = nullptr;
        hr = D3DCompile(PS_HLSL, strlen(PS_HLSL), "PS", nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
        if (FAILED(hr)) {
            printf("[DXGICapture] PS compile failed: %s\n",
                   errBlob ? (char*)errBlob->GetBufferPointer() : "?");
            if (errBlob) errBlob->Release();
            return false;
        }
        if (errBlob) errBlob->Release();
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
        psBlob->Release();
        if (FAILED(hr)) {
            printf("[DXGICapture] CreatePixelShader failed: 0x%08X\n", hr);
            return false;
        }

        // 5. Linear sampler (bilinear)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = device->CreateSamplerState(&sampDesc, &linearSampler);
        if (FAILED(hr)) {
            printf("[DXGICapture] CreateSamplerState failed: 0x%08X\n", hr);
            return false;
        }

        printf("[DXGICapture] GPU scale resources ready: %dx%d -> %dx%d (bilinear)\n",
               srcW, srcH, scaleWidth, scaleHeight);
        scaleReady = true;
        return true;
    }

    void freeScaleResources() {
        scaleReady = false;
        if (linearSampler)  { linearSampler->Release();  linearSampler  = nullptr; }
        if (pixelShader)    { pixelShader->Release();    pixelShader    = nullptr; }
        if (vertexShader)   { vertexShader->Release();   vertexShader   = nullptr; }
        if (scaledRTV)      { scaledRTV->Release();      scaledRTV      = nullptr; }
        if (scaledTexture)  { scaledTexture->Release();  scaledTexture  = nullptr; }
        if (desktopSRV)     { desktopSRV->Release();     desktopSRV     = nullptr; }
        if (desktopCopy)    { desktopCopy->Release();    desktopCopy    = nullptr; }
    }

    // -----------------------------------------------------------------------
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
            width  = outputDesc.DesktopCoordinates.right  - outputDesc.DesktopCoordinates.left;
            height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
        }

        IDXGIOutput1* dxgiOutput1 = nullptr;
        hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
        dxgiOutput->Release();
        if (FAILED(hr)) return false;

        hr = dxgiOutput1->DuplicateOutput(device, &duplication);
        dxgiOutput1->Release();
        if (FAILED(hr)) {
            // 0x80070005 = E_ACCESSDENIED (Virtual desktop switch, UAC, or lock screen)
            if (hr != (HRESULT)0x80070005) {
                printf("[DXGICapture] Failed to recreate Desktop Duplication: 0x%08X\n", hr);
            }
            return false;
        }

        // Recreate staging texture with new dimensions if needed
        createStagingTexture();

        printf("[DXGICapture] Desktop Duplication recovered successfully (%dx%d)!\n", width, height);
        return true;
    }

    // -----------------------------------------------------------------------
    // GPU scale blit: desktopTexture (or sub-region copy) -> scaledTexture
    bool doGpuScale(ID3D11Texture2D* desktopTexture) {
        // Copy capture region of desktop into desktopCopy (SHADER_RESOURCE)
        int srcW = (captureWidth  > 0) ? captureWidth  : width;
        int srcH = (captureHeight > 0) ? captureHeight : height;

        bool isSubRegion = (captureX > 0 || captureY > 0 || srcW < width || srcH < height);
        if (isSubRegion) {
            D3D11_BOX box;
            box.left   = (UINT)captureX;
            box.top    = (UINT)captureY;
            box.front  = 0;
            box.right  = (UINT)(captureX + srcW);
            box.bottom = (UINT)(captureY + srcH);
            box.back   = 1;
            context->CopySubresourceRegion(desktopCopy, 0, 0, 0, 0, desktopTexture, 0, &box);
        } else {
            context->CopyResource(desktopCopy, desktopTexture);
        }

        // Set viewport to target scale
        D3D11_VIEWPORT vp = {};
        vp.Width    = (FLOAT)scaleWidth;
        vp.Height   = (FLOAT)scaleHeight;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports(1, &vp);

        // Bind RTV as render target
        context->OMSetRenderTargets(1, &scaledRTV, nullptr);

        // Bind shaders + SRV + sampler
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader,  nullptr, 0);
        context->PSSetShaderResources(0, 1, &desktopSRV);
        context->PSSetSamplers(0, 1, &linearSampler);

        // Topology: triangle list (3 vertices for full-screen triangle)
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(nullptr);

        // Draw full-screen quad
        context->Draw(3, 0);

        // Unbind RTV (best practice before copying)
        ID3D11RenderTargetView* nullRTV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        return true;
    }

public:
    DXGICapture() {}

    ~DXGICapture() {
        cleanup();
    }

    // -----------------------------------------------------------------------
    // Set GPU scaling target dimensions (0 = disable scaling)
    bool setScale(int scaleW, int scaleH) {
        bool changed = (scaleW != scaleWidth || scaleH != scaleHeight);
        scaleWidth  = scaleW;
        scaleHeight = scaleH;

        if (!device) {
            // Not yet initialized; will be applied at initialization time
            return true;
        }

        if (changed) {
            if (scaleWidth > 0 && scaleHeight > 0) {
                if (!createScaleResources()) return false;
            } else {
                freeScaleResources();
            }
            // Rebuild staging texture to match new output dimensions
            if (!createStagingTexture()) return false;
            int outW = (scaleWidth  > 0) ? scaleWidth  : ((captureWidth  > 0) ? captureWidth  : width);
            int outH = (scaleHeight > 0) ? scaleHeight : ((captureHeight > 0) ? captureHeight : height);
            if (!allocateBufferPool(outW * outH)) return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Dynamic region update without destroying the D3D11 device or duplication session
    bool setRegion(int x, int y, int w, int h) {
        if (w <= 0 || w > width)  w = width;
        if (h <= 0 || h > height) h = height;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > width)  w = width  - x;
        if (y + h > height) h = height - y;

        bool sizeChanged = (w != captureWidth || h != captureHeight);

        captureX      = x;
        captureY      = y;
        captureWidth  = w;
        captureHeight = h;

        if (sizeChanged) {
            // If scaling is active, rebuild scale resources to match new source size
            if (scaleWidth > 0 && scaleHeight > 0 && device) {
                if (!createScaleResources()) return false;
            }
            if (!createStagingTexture()) return false;
            int outW = (scaleWidth  > 0) ? scaleWidth  : w;
            int outH = (scaleHeight > 0) ? scaleHeight : h;
            if (!allocateBufferPool(outW * outH)) return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------
    bool initialize(int monitorIndex = 0, int x = 0, int y = 0, int w = 0, int h = 0) {
        printf("[DXGICapture] Initializing for monitor %d region (%d,%d %dx%d)\n",
               monitorIndex, x, y, w, h);

        HRESULT hr;
        outputIndex   = monitorIndex;
        captureX      = x;
        captureY      = y;
        captureWidth  = w;
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
            width  = outputDesc.DesktopCoordinates.right  - outputDesc.DesktopCoordinates.left;
            height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
            printf("[DXGICapture] Monitor %d: %dx%d\n", monitorIndex, width, height);
        }

        // Validate capture region
        if (captureWidth  <= 0 || captureWidth  > width)  captureWidth  = width;
        if (captureHeight <= 0 || captureHeight > height) captureHeight = height;
        if (captureX < 0) captureX = 0;
        if (captureY < 0) captureY = 0;
        if (captureX + captureWidth  > width)  captureWidth  = width  - captureX;
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
            if (device)  { device->Release();  device  = nullptr; }
        }

        // Build GPU scale resources if scale was pre-configured
        if (!useGdiFallback && scaleWidth > 0 && scaleHeight > 0) {
            if (!createScaleResources()) {
                printf("[DXGICapture] GPU scale init failed, falling back to no scaling\n");
                scaleWidth = scaleHeight = 0;
            }
        }

        // Create staging texture (sized to output after scaling if applicable)
        if (!createStagingTexture()) {
            return false;
        }

        // Initialize 64-byte aligned frame pool
        int outW = (scaleWidth  > 0) ? scaleWidth  : captureWidth;
        int outH = (scaleHeight > 0) ? scaleHeight : captureHeight;
        if (!allocateBufferPool(outW * outH)) {
            return false;
        }

        printf("[DXGICapture] Initialization complete (output %dx%d)\n", outW, outH);
        return true;
    }

    // -----------------------------------------------------------------------
    bool captureFrame(int** pixels, int* outWidth, int* outHeight) {
        if (useGdiFallback) {
            int outW = (captureWidth  > 0) ? captureWidth  : width;
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

            *pixels    = pixelBuffer;
            *outWidth  = outW;
            *outHeight = outH;
            return true;
        }

        if (!device || !context) {
            return false;
        }

        if (isAccessLost || !duplication) {
            ULONGLONG now = GetTickCount64();
            if (now - lastRecoveryAttempt < 250) {
                // Throttle recovery polling while on another desktop or locked
                return false;
            }
            lastRecoveryAttempt = now;
            if (recreateDuplication()) {
                isAccessLost = false;
                // Recreate scale resources after recovery (device may have changed)
                if (scaleWidth > 0 && scaleHeight > 0) {
                    createScaleResources();
                }
            } else {
                return false;
            }
        }

        IDXGIResource* desktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        // Non-blocking acquire next frame (0ms timeout)
        HRESULT hr = duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false; // No new frame
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            isAccessLost = true;
            lastRecoveryAttempt = GetTickCount64();
            if (duplication) {
                duplication->Release();
                duplication = nullptr;
            }
            return false;
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

        // ----------------------------------------------------------------
        // GPU SCALING PATH
        // ----------------------------------------------------------------
        if (scaleReady && scaleWidth > 0 && scaleHeight > 0) {
            bool ok = doGpuScale(desktopTexture);
            desktopTexture->Release();

            if (!ok) {
                duplication->ReleaseFrame();
                return false;
            }

            // Copy scaledTexture -> stagingTexture for CPU readback
            context->CopyResource(stagingTexture, scaledTexture);

            D3D11_MAPPED_SUBRESOURCE mapped;
            hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                duplication->ReleaseFrame();
                return false;
            }

            pixelBuffer = bufferPool[poolIndex];
            poolIndex = (poolIndex + 1) % POOL_SIZE;

            BYTE* src = (BYTE*)mapped.pData;
            const int rowBytes = scaleWidth * 4;
            if ((UINT)rowBytes == mapped.RowPitch) {
                memcpy(pixelBuffer, src, (size_t)scaleWidth * scaleHeight * 4);
            } else {
                BYTE* dst = (BYTE*)pixelBuffer;
                for (int y = 0; y < scaleHeight; y++) {
                    memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
                }
            }

            context->Unmap(stagingTexture, 0);
            duplication->ReleaseFrame();

            *pixels    = pixelBuffer;
            *outWidth  = scaleWidth;
            *outHeight = scaleHeight;
            return true;
        }

        // ----------------------------------------------------------------
        // NON-SCALING PATH (original behavior)
        // ----------------------------------------------------------------
        int outW = (captureWidth  > 0) ? captureWidth  : width;
        int outH = (captureHeight > 0) ? captureHeight : height;

        bool isSubRegion = (captureX > 0 || captureY > 0 || outW < width || outH < height);
        if (isSubRegion) {
            D3D11_BOX box;
            box.left   = (UINT)captureX;
            box.top    = (UINT)captureY;
            box.front  = 0;
            box.right  = (UINT)(captureX + outW);
            box.bottom = (UINT)(captureY + outH);
            box.back   = 1;
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
        const int rowBytes = outW * 4;
        if (mappedResource.RowPitch == (UINT)rowBytes) {
            // Contiguous direct memory copy: 0.2 ms for 1080p/3K
            memcpy(pixelBuffer, srcPixels, (size_t)outW * outH * 4);
        } else {
            // Row-by-row fast memcpy (handles GPU memory pitch alignment padding)
            BYTE* dst = (BYTE*)pixelBuffer;
            for (int y = 0; y < outH; y++) {
                memcpy(dst + y * rowBytes, srcPixels + y * mappedResource.RowPitch, rowBytes);
            }
        }

        context->Unmap(stagingTexture, 0);
        duplication->ReleaseFrame();

        *pixels    = pixelBuffer;
        *outWidth  = outW;
        *outHeight = outH;
        return true;
    }

    // -----------------------------------------------------------------------
    void cleanup() {
        freeScaleResources();
        freeBufferPool();

        if (stagingTexture) { stagingTexture->Release(); stagingTexture = nullptr; }
        if (duplication)    { duplication->Release();    duplication    = nullptr; }
        if (context)        { context->Release();        context        = nullptr; }
        if (device)         { device->Release();         device         = nullptr; }
        if (hBitmap)        { DeleteObject(hBitmap);     hBitmap        = nullptr; }
        if (hdcMem)         { DeleteDC(hdcMem);          hdcMem         = nullptr; }
        if (hdcScreen)      { ReleaseDC(NULL, hdcScreen); hdcScreen     = nullptr; }

        gdiPixels      = nullptr;
        useGdiFallback = false;
        width          = 0;
        height         = 0;
        bufferSize     = 0;
    }

    int getWidth()  const {
        if (scaleWidth  > 0) return scaleWidth;
        return (captureWidth  > 0) ? captureWidth  : width;
    }
    int getHeight() const {
        if (scaleHeight > 0) return scaleHeight;
        return (captureHeight > 0) ? captureHeight : height;
    }
};

// ===========================================================================
// C interface for JNI
// ===========================================================================
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

    bool dxgiSetScale(void* capture, int scaleW, int scaleH) {
        if (!capture) return false;
        return static_cast<DXGICapture*>(capture)->setScale(scaleW, scaleH);
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
