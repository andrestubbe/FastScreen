/*
 * FastScreen - JNI Implementation
 * 
 * High-performance Java screen capture using DXGI Desktop Duplication
 */

#include "fastscreen.h"
#include <stdio.h>
#include <windows.h>

// Forward declarations from DXGICapture.cpp
extern "C" {
    void* dxgiCreateCapture();
    bool dxgiInitialize(void* capture, int monitorIndex);
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height);
    void dxgiDestroyCapture(void* capture);
}

// Global capture instance for single-capture mode
static void* g_capture = nullptr;
static int g_width = 0;
static int g_height = 0;

// Streaming state
static void* g_streamCapture = nullptr;
static int g_streamX = 0;
static int g_streamY = 0;
static int g_streamWidth = 0;
static int g_streamHeight = 0;
static bool g_streaming = false;

JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv* env, jobject obj) {
    printf("[FastScreen] Native initialization\n");
    
    // Create global capture instance
    if (!g_capture) {
        g_capture = dxgiCreateCapture();
        if (!dxgiInitialize(g_capture, 0)) {
            printf("[FastScreen] Failed to initialize DXGI capture\n");
            dxgiDestroyCapture(g_capture);
            g_capture = nullptr;
            return 0;
        }
    }
    
    return (jlong)g_capture;
}

JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeCaptureScreen(
    JNIEnv* env, jobject obj, 
    jint x, jint y, jint width, jint height) {
    
    if (!g_capture) {
        printf("[FastScreen] Not initialized\n");
        return nullptr;
    }
    
    // For now, capture full screen (region cropping can be added)
    int* pixels = nullptr;
    int capturedWidth = 0;
    int capturedHeight = 0;
    
    if (!dxgiCaptureFrame(g_capture, &pixels, &capturedWidth, &capturedHeight)) {
        // No new frame available
        return nullptr;
    }
    
    // Create Java array
    jintArray result = env->NewIntArray(capturedWidth * capturedHeight);
    if (result == nullptr) {
        return nullptr;
    }
    
    // Copy pixels to Java array
    env->SetIntArrayRegion(result, 0, capturedWidth * capturedHeight, (jint*)pixels);
    
    return result;
}

JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(
    JNIEnv* env, jobject obj,
    jint x, jint y, jint width, jint height) {
    
    printf("[FastScreen] Starting stream: %dx%d @ (%d,%d)\n", width, height, x, y);
    
    if (g_streaming) {
        printf("[FastScreen] Stream already active\n");
        return JNI_FALSE;
    }
    
    // Create separate capture instance for streaming
    g_streamCapture = dxgiCreateCapture();
    if (!dxgiInitialize(g_streamCapture, 0)) {
        printf("[FastScreen] Failed to initialize stream capture\n");
        dxgiDestroyCapture(g_streamCapture);
        g_streamCapture = nullptr;
        return JNI_FALSE;
    }
    
    g_streamX = x;
    g_streamY = y;
    g_streamWidth = width;
    g_streamHeight = height;
    g_streaming = true;
    
    return JNI_TRUE;
}

JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeGetNextFrame(JNIEnv* env, jobject obj) {
    if (!g_streaming || !g_streamCapture) {
        return nullptr;
    }
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(g_streamCapture, &pixels, &width, &height)) {
        // No new frame available - return empty array or null
        return nullptr;
    }
    
    // Create Java array
    jintArray result = env->NewIntArray(width * height);
    if (result == nullptr) {
        return nullptr;
    }
    
    // Copy pixels
    env->SetIntArrayRegion(result, 0, width * height, (jint*)pixels);
    
    return result;
}

JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(JNIEnv* env, jobject obj) {
    printf("[FastScreen] Stopping stream\n");
    
    if (g_streamCapture) {
        dxgiDestroyCapture(g_streamCapture);
        g_streamCapture = nullptr;
    }
    
    g_streaming = false;
    g_streamWidth = 0;
    g_streamHeight = 0;
}

JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(
    JNIEnv* env, jobject obj, jint x, jint y) {
    
    // For single pixel, we could optimize with a smaller capture
    // For now, use full frame capture
    if (!g_capture) {
        return 0;
    }
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(g_capture, &pixels, &width, &height)) {
        return 0;
    }
    
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return pixels[y * width + x];
    }
    
    return 0;
}

JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeDispose(JNIEnv* env, jobject obj, jlong handle) {
    printf("[FastScreen] Disposing native resources\n");
    
    if (g_streaming) {
        Java_fastscreen_FastScreen_nativeStopStream(env, obj);
    }
    
    if (g_capture) {
        dxgiDestroyCapture(g_capture);
        g_capture = nullptr;
    }
}

JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(JNIEnv* env, jobject obj) {
    // Enumerate DXGI adapters and outputs
    // For now, return 1 (primary monitor)
    // TODO: Implement full enumeration
    return 1;
}
