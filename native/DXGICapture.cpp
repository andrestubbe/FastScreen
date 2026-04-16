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

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class DXGICapture {
private:
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    
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
    
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height) {
        return static_cast<DXGICapture*>(capture)->captureFrame(pixels, width, height);
    }
    
    void dxgiDestroyCapture(void* capture) {
        delete static_cast<DXGICapture*>(capture);
    }
    
}
