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
    int width = 0;
    int height = 0;
    int* pixelBuffer = nullptr;
    int bufferSize = 0;
    
    bool createStagingTexture() {
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }
        
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
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
            printf("[DXGICapture] Failed to create staging texture: 0x%08X\n", hr);
            return false;
        }
        
        return true;
    }
    
public:
    DXGICapture() {}
    
    ~DXGICapture() {
        cleanup();
    }
    
    bool initialize(int monitorIndex = 0) {
        printf("[DXGICapture] Initializing for monitor %d\n", monitorIndex);
        
        HRESULT hr;
        
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
            } else {
                printf("[DXGICapture] Failed to create duplication: 0x%08X\n", hr);
            }
            return false;
        }
        
        // Create staging texture
        if (!createStagingTexture()) {
            return false;
        }
        
        // Allocate pixel buffer (RGBA)
        bufferSize = width * height;
        pixelBuffer = (int*)malloc(bufferSize * sizeof(int));
        if (!pixelBuffer) {
            printf("[DXGICapture] Failed to allocate pixel buffer\n");
            return false;
        }
        
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
        
        // Convert BGRA to RGBA
        BYTE* srcPixels = (BYTE*)mappedResource.pData;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int srcIdx = y * mappedResource.RowPitch + x * 4;
                int dstIdx = y * width + x;
                
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
        *outWidth = width;
        *outHeight = height;
        
        return true;
    }
    
    void cleanup() {
        if (pixelBuffer) {
            free(pixelBuffer);
            pixelBuffer = nullptr;
        }
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
    
    bool dxgiInitialize(void* capture, int monitorIndex) {
        return static_cast<DXGICapture*>(capture)->initialize(monitorIndex);
    }
    
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height) {
        return static_cast<DXGICapture*>(capture)->captureFrame(pixels, width, height);
    }
    
    void dxgiDestroyCapture(void* capture) {
        delete static_cast<DXGICapture*>(capture);
    }
    
}
