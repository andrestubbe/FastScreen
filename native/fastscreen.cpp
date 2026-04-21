/**
 * @file fastscreen.cpp
 * @brief FastScreen JNI Implementation - DXGI screen capture
 * 
 * @details Implements hardware-accelerated screen capture using DXGI Desktop
 * Duplication API. Provides both single-frame capture and continuous streaming
 * modes with optional hardware scaling.
 * 
 * @par Implementation Notes
 * - Uses global capture state for single-capture mode
 * - Separate capture instance for streaming mode
 * - Delegates to DXGICapture class for actual GPU operations
 * - Zero-copy DirectByteBuffer support for streaming
 * 
 * @author FastJava Team
 * @version 1.0.0
 * @copyright MIT License
 */

#include "fastscreen.h"
#include <stdio.h>
#include <windows.h>

// Forward declarations from DXGICapture.cpp
extern "C" {
    void* dxgiCreateCapture();
    bool dxgiInitialize(void* capture, int monitorIndex);
    bool dxgiInitializeRegion(void* capture, int monitorIndex, int x, int y, int w, int h);
    bool dxgiSetupScaling(void* capture, int outW, int outH, int filter);
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height);
    void dxgiDestroyCapture(void* capture);
}

/// @name Global State
/// @brief Global capture state for single-frame mode
/// @{
static void* g_capture = nullptr;      /**< DXGI capture instance (single mode) */
static int g_width = 0;                /**< Capture width in pixels */
static int g_height = 0;               /**< Capture height in pixels */
/// @}

/// @name Streaming State
/// @brief Separate state for continuous streaming mode
/// @{
static void* g_streamCapture = nullptr; /**< DXGI capture instance (streaming) */
static int g_streamX = 0;               /**< Stream region X coordinate */
static int g_streamY = 0;               /**< Stream region Y coordinate */
static int g_streamWidth = 0;           /**< Stream region width */
static int g_streamHeight = 0;          /**< Stream region height */
static bool g_streaming = false;        /**< True if streaming is active */
/// @}

/**
 * @brief Initialize native capture for full screen
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @return jlong Native handle, or 0 on failure
 * @note Creates global capture instance for primary monitor
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv* env, jobject obj) {
    printf("[FastScreen] Native initialization\n");
    
    // Create global capture instance (full screen)
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

/**
 * @brief Initialize native capture for specific screen region
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param x Region X coordinate
 * @param y Region Y coordinate
 * @param w Region width
 * @param h Region height
 * @return jlong Native handle, or 0 on failure
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInitRegion(JNIEnv* env, jobject obj, jint x, jint y, jint w, jint h) {
    printf("[FastScreen] Native initialization region (%d,%d %dx%d)\n", x, y, w, h);
    
    // Create global capture instance with region
    if (!g_capture) {
        g_capture = dxgiCreateCapture();
        if (!dxgiInitializeRegion(g_capture, 0, x, y, w, h)) {
            printf("[FastScreen] Failed to initialize DXGI capture region\n");
            dxgiDestroyCapture(g_capture);
            g_capture = nullptr;
            return 0;
        }
    }
    
    return (jlong)g_capture;
}

/**
 * @brief Capture single frame as RGBA int array
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param x Capture X offset
 * @param y Capture Y offset
 * @param width Capture width
 * @param height Capture height
 * @return jintArray RGBA pixel data, or null if no new frame
 */
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

/**
 * @brief Start continuous streaming capture mode
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param x Stream region X
 * @param y Stream region Y
 * @param width Stream region width
 * @param height Stream region height
 * @return jboolean JNI_TRUE if streaming started
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(
    JNIEnv* env, jobject obj,
    jint x, jint y, jint width, jint height) {
    
    printf("[FastScreen] Starting stream: %dx%d @ (%d,%d)\n", width, height, x, y);
    
    if (g_streaming) {
        printf("[FastScreen] Stream already active\n");
        return JNI_FALSE;
    }
    
    // Try to create region-based capture for streaming
    // Note: DXGI Desktop Duplication can only have ONE per monitor
    // So we try to create a new one, but if it fails (e.g., virtual desktop),
    // we fall back to using the global capture
    g_streamCapture = dxgiCreateCapture();
    if (!dxgiInitializeRegion(g_streamCapture, 0, x, y, width, height)) {
        printf("[FastScreen] Region capture failed, trying to reuse global capture\n");
        dxgiDestroyCapture(g_streamCapture);
        
        // Fall back to global capture (if it exists)
        if (g_capture) {
            printf("[FastScreen] Using global capture for streaming\n");
            g_streamCapture = g_capture;
        } else {
            printf("[FastScreen] No global capture available\n");
            g_streamCapture = nullptr;
            return JNI_FALSE;
        }
    }
    
    g_streamX = x;
    g_streamY = y;
    g_streamWidth = width;
    g_streamHeight = height;
    g_streaming = true;
    
    return JNI_TRUE;
}

/**
 * @brief Get next frame from streaming capture (int array)
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @return jintArray RGBA pixel data, or null if no new frame
 */
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

/**
 * @brief Get next frame as DirectByteBuffer (zero-copy)
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @return jobject DirectByteBuffer pointing to native pixel data
 * @note Zero-copy: Java can read directly from native memory
 */
JNIEXPORT jobject JNICALL Java_fastscreen_FastScreen_nativeGetNextFrameDirect(JNIEnv* env, jobject obj) {
    if (!g_streaming || !g_streamCapture) {
        return nullptr;
    }
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(g_streamCapture, &pixels, &width, &height)) {
        return nullptr;
    }
    
    // Create DirectByteBuffer pointing directly to native memory
    // NO COPY! Java can read/write directly to this memory
    return env->NewDirectByteBuffer(pixels, width * height * 4);
}

/**
 * @brief Stop streaming capture mode
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(JNIEnv* env, jobject obj) {
    printf("[FastScreen] Stopping stream\n");
    
    // Only destroy if it's a separate capture (not the global one)
    if (g_streamCapture && g_streamCapture != g_capture) {
        dxgiDestroyCapture(g_streamCapture);
    }
    g_streamCapture = nullptr;
    
    g_streaming = false;
    g_streamWidth = 0;
    g_streamHeight = 0;
}

/**
 * @brief Configure hardware scaling for streaming
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param outW Output width
 * @param outH Output height
 * @param filter Filter mode (0=Point, 1=Linear)
 * @return jboolean JNI_TRUE if scaling configured
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetupHardwareScaling(
    JNIEnv* env, jobject obj, jint outW, jint outH, jint filter) {
    
    printf("[FastScreen] Setting up hardware scaling: %dx%d -> %dx%d (filter: %d)\n",
           g_streamWidth, g_streamHeight, outW, outH, filter);
    
    if (!g_streamCapture) {
        printf("[FastScreen] No stream capture available for scaling\n");
        return JNI_FALSE;
    }
    
    bool success = dxgiSetupScaling(g_streamCapture, outW, outH, filter);
    if (success) {
        // Update stream dimensions to match output
        g_streamWidth = outW;
        g_streamHeight = outH;
    }
    
    return success ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Get color of single pixel at coordinates
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @return jint RGBA color value
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(
    JNIEnv* env, jobject obj, jint x, jint y) {
    
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

/**
 * @brief Release all native resources
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @param handle Native handle (unused)
 */
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

/**
 * @brief Get number of connected monitors
 * @param env JNI environment pointer
 * @param obj Java FastScreen object
 * @return jint Number of monitors (currently returns 1)
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(JNIEnv* env, jobject obj) {
    return 1;
}
