/**
 * @file fastscreen.h
 * @brief FastScreen JNI Header - High-performance screen capture
 * 
 * @details Provides JNI declarations for hardware-accelerated screen capture
 * using DirectX Graphics Infrastructure (DXGI) Desktop Duplication API.
 * Supports full screen, region capture, streaming, and hardware scaling.
 * 
 * @par Architecture
 * - DXGI Desktop Duplication for GPU-accelerated capture
 * - Direct3D 11 for hardware scaling and format conversion
 * - Triple buffering for smooth streaming
 * - Zero-copy DirectByteBuffer support
 * 
 * @par Features
 * - Full screen capture with minimal latency
 * - Region-based capture (sub-rectangles)
 * - Continuous streaming mode for video
 * - Hardware scaling (Point/Linear filter)
 * - Multi-monitor support
 * 
 * @par Platform Requirements
 * - Windows 8 or later (DXGI 1.2+)
 * - Direct3D 11 capable GPU
 * - Desktop Composition enabled
 * 
 * @author FastJava Team
 * @version 1.0.0
 * @copyright MIT License
 */

#ifndef FASTSCREEN_H
#define FASTSCREEN_H

#include <jni.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup JNI_Initialization Initialization
 *  @brief JNI functions for capture initialization
 *  @{ */

/**
 * @brief Initialize native capture for full screen
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @return jlong Native handle (DXGICapture*), or 0 on failure
 * @note Creates DXGI capture instance for primary monitor
 * @see Java_fastscreen_FastScreen_nativeInitRegion
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv* env, jobject obj);

/**
 * @brief Initialize native capture for specific screen region
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param x Region X coordinate
 * @param y Region Y coordinate
 * @param w Region width
 * @param h Region height
 * @return jlong Native handle, or 0 on failure
 * @note Region is clipped to monitor bounds automatically
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInitRegion(JNIEnv* env, jobject obj, jint x, jint y, jint w, jint h);

/**
 * @brief Capture single frame as RGBA int array
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param x Capture X offset
 * @param y Capture Y offset
 * @param width Capture width
 * @param height Capture height
 * @return jintArray RGBA pixel data, or null if no new frame
 * @note Returns null if desktop hasn't changed
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeCaptureScreen(JNIEnv* env, jobject obj, jint x, jint y, jint width, jint height);

/** @} */

/** @defgroup JNI_Streaming Streaming
 *  @brief JNI functions for continuous capture
 *  @{ */

/**
 * @brief Start continuous streaming capture mode
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param x Stream region X
 * @param y Stream region Y
 * @param width Stream region width
 * @param height Stream region height
 * @return jboolean JNI_TRUE if streaming started
 * @note Creates separate capture instance for streaming
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(JNIEnv* env, jobject obj, jint x, jint y, jint width, jint height);

/**
 * @brief Get next frame from streaming capture (int array)
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @return jintArray RGBA pixel data, or null if no new frame
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeGetNextFrame(JNIEnv* env, jobject obj);

/**
 * @brief Stop streaming capture mode
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(JNIEnv* env, jobject obj);

/** @} */

/** @defgroup JNI_Utilities Utilities
 *  @brief JNI helper functions
 *  @{ */

/**
 * @brief Get color of single pixel at coordinates
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @return jint RGBA color value
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(JNIEnv* env, jobject obj, jint x, jint y);

/**
 * @brief Release all native resources
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle (unused)
 * @note Stops streaming and releases all resources
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeDispose(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Get number of connected monitors
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @return jint Number of monitors
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(JNIEnv* env, jobject obj);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* FASTSCREEN_H */
