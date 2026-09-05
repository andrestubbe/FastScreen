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
 * @brief Initialize native capture for full screen on specified monitor
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param monitorIndex 0-based monitor index
 * @return jlong Native handle (DXGICapture*), or 0 on failure
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv* env, jobject obj, jint monitorIndex);

/**
 * @brief Initialize native capture for specific screen region
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param monitorIndex 0-based monitor index
 * @param x Region X coordinate
 * @param y Region Y coordinate
 * @param w Region width
 * @param h Region height
 * @return jlong Native handle, or 0 on failure
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInitRegion(JNIEnv* env, jobject obj, jint monitorIndex, jint x, jint y, jint w, jint h);

/**
 * @brief Dynamically set capture region on existing capture instance
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param x Region X coordinate
 * @param y Region Y coordinate
 * @param w Region width
 * @param h Region height
 * @return jboolean JNI_TRUE if region was updated
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetRegion(JNIEnv* env, jobject obj, jlong handle, jint x, jint y, jint w, jint h);

/**
 * @brief Capture single frame as RGBA int array
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param x Capture X offset
 * @param y Capture Y offset
 * @param width Capture width
 * @param height Capture height
 * @return jintArray RGBA pixel data, or null if no new frame
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeCaptureScreen(JNIEnv* env, jobject obj, jlong handle, jint x, jint y, jint width, jint height);

/** @} */

/** @defgroup JNI_Streaming Streaming
 *  @brief JNI functions for continuous capture
 *  @{ */

/**
 * @brief Start continuous streaming capture mode
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param x Stream region X
 * @param y Stream region Y
 * @param width Stream region width
 * @param height Stream region height
 * @return jboolean JNI_TRUE if streaming started
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(JNIEnv* env, jobject obj, jlong handle, jint x, jint y, jint width, jint height);

/**
 * @brief Poll if a new frame is available without allocating an int array
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @return jboolean JNI_TRUE if frame acquired, JNI_FALSE otherwise
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativePollNewFrame(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Copy next frame into pre-allocated user array (0 GC allocations)
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param destArray Destination int[] array
 * @return jboolean JNI_TRUE if copied, JNI_FALSE if no new frame
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeGetNextFrameInto(JNIEnv* env, jobject obj, jlong handle, jintArray destArray);

/**
 * @brief Get next frame from streaming capture (int array)
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @return jintArray RGBA pixel data, or null if no new frame
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeGetNextFrame(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Get next frame as DirectByteBuffer (zero-copy)
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @return jobject DirectByteBuffer pointing to native pixel data
 */
JNIEXPORT jobject JNICALL Java_fastscreen_FastScreen_nativeGetNextFrameDirect(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Stop streaming capture mode
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Configure hardware scaling for streaming
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param outW Output width
 * @param outH Output height
 * @param filter Filter mode (0=Point, 1=Linear)
 * @return jboolean JNI_TRUE if scaling configured
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetupHardwareScaling(JNIEnv* env, jobject obj, jlong handle, jint outW, jint outH, jint filter);

/** @} */

/** @defgroup JNI_Utilities Utilities
 *  @brief JNI helper functions
 *  @{ */

/**
 * @brief Get color of single pixel at coordinates
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 * @param x Pixel X coordinate
 * @param y Pixel Y coordinate
 * @return jint RGBA color value
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(JNIEnv* env, jobject obj, jlong handle, jint x, jint y);

/**
 * @brief Release all native resources
 * @param env JNI environment pointer
 * @param obj FastScreen Java object
 * @param handle Native handle
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeDispose(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Get number of connected monitors
 * @param env JNI environment pointer
 * @param cls FastScreen Java class
 * @return jint Number of monitors
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(JNIEnv* env, jclass cls);

/**
 * @brief Get current frame width
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetFrameWidth(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Get current frame height
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetFrameHeight(JNIEnv* env, jobject obj, jlong handle);

/**
 * @brief Set window display affinity to exclude or include from screen capture
 * @param env JNI environment pointer
 * @param cls FastScreen Java class
 * @param hwnd Window handle (HWND as jlong)
 * @param exclude JNI_TRUE to exclude (WDA_EXCLUDEFROMCAPTURE), JNI_FALSE to include (WDA_NONE)
 * @return jboolean JNI_TRUE if successfully applied
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetWindowExcluded(JNIEnv* env, jclass cls, jlong hwnd, jboolean exclude);

/**
 * @brief Find window by title and set display affinity to exclude or include from screen capture
 * @param env JNI environment pointer
 * @param cls FastScreen Java class
 * @param title Window title substring or exact title
 * @param exclude JNI_TRUE to exclude, JNI_FALSE to include
 * @return jboolean JNI_TRUE if window found and affinity applied
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetWindowExcludedByTitle(JNIEnv* env, jclass cls, jstring title, jboolean exclude);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* FASTSCREEN_H */
