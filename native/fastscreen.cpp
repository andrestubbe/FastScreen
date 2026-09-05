/**
 * @file fastscreen.cpp
 * @brief FastScreen JNI Implementation - DXGI screen capture
 * 
 * @details Implements hardware-accelerated screen capture using DXGI Desktop
 * Duplication API. Eliminates global state by passing native handles (DXGICapture*)
 * from Java, allowing multiple instances, zero race conditions, and isolated streaming.
 * 
 * @author FastJava Team
 * @version 0.1.3
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
    bool dxgiSetRegion(void* capture, int x, int y, int w, int h);
    bool dxgiSetupScaling(void* capture, int outW, int outH, int filter);
    bool dxgiCaptureFrame(void* capture, int** pixels, int* width, int* height);
    int dxgiGetWidth(void* capture);
    int dxgiGetHeight(void* capture);
    void dxgiDestroyCapture(void* capture);
    int dxgiQueryMonitorCount();
}

/**
 * @brief Initialize native capture for full screen
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv* env, jobject obj, jint monitorIndex) {
    void* capture = dxgiCreateCapture();
    if (!capture) {
        return 0;
    }
    if (!dxgiInitialize(capture, monitorIndex)) {
        dxgiDestroyCapture(capture);
        return 0;
    }
    return (jlong)capture;
}

/**
 * @brief Initialize native capture for specific screen region
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInitRegion(
    JNIEnv* env, jobject obj, jint monitorIndex, jint x, jint y, jint w, jint h) {
    void* capture = dxgiCreateCapture();
    if (!capture) {
        return 0;
    }
    if (!dxgiInitializeRegion(capture, monitorIndex, x, y, w, h)) {
        dxgiDestroyCapture(capture);
        return 0;
    }
    return (jlong)capture;
}

/**
 * @brief Dynamically set capture region on existing capture instance
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetRegion(
    JNIEnv* env, jobject obj, jlong handle, jint x, jint y, jint w, jint h) {
    if (!handle) return JNI_FALSE;
    bool success = dxgiSetRegion((void*)handle, x, y, w, h);
    return success ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Capture single frame as RGBA int array
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeCaptureScreen(
    JNIEnv* env, jobject obj, jlong handle,
    jint x, jint y, jint width, jint height) {
    
    if (!handle) return nullptr;
    void* capture = (void*)handle;
    
    int* pixels = nullptr;
    int capturedWidth = 0;
    int capturedHeight = 0;
    
    if (!dxgiCaptureFrame(capture, &pixels, &capturedWidth, &capturedHeight)) {
        return nullptr;
    }
    
    jintArray result = env->NewIntArray(capturedWidth * capturedHeight);
    if (!result) return nullptr;
    
    env->SetIntArrayRegion(result, 0, capturedWidth * capturedHeight, (jint*)pixels);
    return result;
}

/**
 * @brief Start continuous streaming capture mode
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(
    JNIEnv* env, jobject obj, jlong handle,
    jint x, jint y, jint width, jint height) {
    
    if (!handle) return JNI_FALSE;
    void* capture = (void*)handle;
    
    bool ok = dxgiSetRegion(capture, x, y, width, height);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Poll if a new frame is available without allocating an int array
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativePollNewFrame(
    JNIEnv* env, jobject obj, jlong handle) {
    if (!handle) return JNI_FALSE;
    void* capture = (void*)handle;

    int* pixels = nullptr;
    int width = 0;
    int height = 0;

    if (!dxgiCaptureFrame(capture, &pixels, &width, &height)) {
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

/**
 * @brief Copy next frame into pre-allocated user array (0 GC allocations)
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeGetNextFrameInto(
    JNIEnv* env, jobject obj, jlong handle, jintArray destArray) {
    if (!handle || !destArray) return JNI_FALSE;
    void* capture = (void*)handle;

    int* pixels = nullptr;
    int width = 0;
    int height = 0;

    if (!dxgiCaptureFrame(capture, &pixels, &width, &height)) {
        return JNI_FALSE;
    }

    jsize len = env->GetArrayLength(destArray);
    int total = width * height;
    if (len < total) {
        return JNI_FALSE;
    }

    env->SetIntArrayRegion(destArray, 0, total, (jint*)pixels);
    return JNI_TRUE;
}

/**
 * @brief Get next frame from streaming capture (int array)
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeGetNextFrame(
    JNIEnv* env, jobject obj, jlong handle) {
    
    if (!handle) return nullptr;
    void* capture = (void*)handle;
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(capture, &pixels, &width, &height)) {
        return nullptr;
    }
    
    jintArray result = env->NewIntArray(width * height);
    if (!result) return nullptr;
    
    env->SetIntArrayRegion(result, 0, width * height, (jint*)pixels);
    return result;
}

/**
 * @brief Get next frame as DirectByteBuffer (zero-copy)
 */
JNIEXPORT jobject JNICALL Java_fastscreen_FastScreen_nativeGetNextFrameDirect(
    JNIEnv* env, jobject obj, jlong handle) {
    
    if (!handle) return nullptr;
    void* capture = (void*)handle;
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(capture, &pixels, &width, &height)) {
        return nullptr;
    }
    
    return env->NewDirectByteBuffer(pixels, width * height * 4);
}

/**
 * @brief Stop streaming capture mode
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(
    JNIEnv* env, jobject obj, jlong handle) {
    // Streaming state is managed on the Java side; handle remains valid until dispose
}

/**
 * @brief Configure hardware scaling for streaming
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetupHardwareScaling(
    JNIEnv* env, jobject obj, jlong handle, jint outW, jint outH, jint filter) {
    
    if (!handle) return JNI_FALSE;
    bool success = dxgiSetupScaling((void*)handle, outW, outH, filter);
    return success ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Get color of single pixel at coordinates
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(
    JNIEnv* env, jobject obj, jlong handle, jint x, jint y) {
    
    if (!handle) return 0;
    void* capture = (void*)handle;
    
    int* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    if (!dxgiCaptureFrame(capture, &pixels, &width, &height)) {
        return 0;
    }
    
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return pixels[y * width + x];
    }
    
    return 0;
}

/**
 * @brief Release all native resources
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeDispose(
    JNIEnv* env, jobject obj, jlong handle) {
    if (handle) {
        dxgiDestroyCapture((void*)handle);
    }
}

/**
 * @brief Get number of connected monitors
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(
    JNIEnv* env, jclass cls) {
    return (jint)dxgiQueryMonitorCount();
}

JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetFrameWidth(
    JNIEnv* env, jobject obj, jlong handle) {
    if (!handle) return 0;
    return (jint)dxgiGetWidth((void*)handle);
}

JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetFrameHeight(
    JNIEnv* env, jobject obj, jlong handle) {
    if (!handle) return 0;
    return (jint)dxgiGetHeight((void*)handle);
}

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

/**
 * @brief Exclude or include window from capture by HWND
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetWindowExcluded(
    JNIEnv* env, jclass cls, jlong hwnd, jboolean exclude) {
    if (!hwnd) {
        return JNI_FALSE;
    }
    HWND h = (HWND)hwnd;
    DWORD affinity = exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    BOOL res = SetWindowDisplayAffinity(h, affinity);
    return res ? JNI_TRUE : JNI_FALSE;
}

struct FastScreenEnumDataW {
    const wchar_t* targetTitle;
    HWND foundHwnd;
};

static BOOL CALLBACK FastScreenEnumWindowsProcW(HWND hwnd, LPARAM lParam) {
    FastScreenEnumDataW* data = (FastScreenEnumDataW*)lParam;
    wchar_t title[512];
    if (GetWindowTextW(hwnd, title, sizeof(title)/sizeof(wchar_t)) > 0) {
        if (wcsstr(title, data->targetTitle) != nullptr) {
            data->foundHwnd = hwnd;
            return FALSE; // stop search
        }
    }
    return TRUE; // continue search
}

/**
 * @brief Exclude or include window from capture by window title (Unicode safe)
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeSetWindowExcludedByTitle(
    JNIEnv* env, jclass cls, jstring title, jboolean exclude) {
    if (!title) {
        return JNI_FALSE;
    }
    const jchar* chars = env->GetStringChars(title, nullptr);
    if (!chars) {
        return JNI_FALSE;
    }

    const wchar_t* wstr = (const wchar_t*)chars;
    HWND hwnd = FindWindowW(nullptr, wstr);
    if (!hwnd) {
        FastScreenEnumDataW data = { wstr, nullptr };
        EnumWindows(FastScreenEnumWindowsProcW, (LPARAM)&data);
        hwnd = data.foundHwnd;
    }
    env->ReleaseStringChars(title, chars);

    if (hwnd) {
        DWORD affinity = exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
        BOOL res = SetWindowDisplayAffinity(hwnd, affinity);
        return res ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}


