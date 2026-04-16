/*
 * FastScreen - JNI Header
 * 
 * High-performance Java screen capture using DXGI Desktop Duplication
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

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeInit
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_fastscreen_FastScreen_nativeInit(JNIEnv *, jobject);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeCaptureScreen
 * Signature: (IIII)[I
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeCaptureScreen(JNIEnv *, jobject, jint, jint, jint, jint);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeStartStream
 * Signature: (IIII)Z
 */
JNIEXPORT jboolean JNICALL Java_fastscreen_FastScreen_nativeStartStream(JNIEnv *, jobject, jint, jint, jint, jint);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeGetNextFrame
 * Signature: ()[I
 */
JNIEXPORT jintArray JNICALL Java_fastscreen_FastScreen_nativeGetNextFrame(JNIEnv *, jobject);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeStopStream
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeStopStream(JNIEnv *, jobject);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeGetPixelColor
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetPixelColor(JNIEnv *, jobject, jint, jint);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeDispose
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_fastscreen_FastScreen_nativeDispose(JNIEnv *, jobject, jlong);

/*
 * Class:     fastscreen_FastScreen
 * Method:    nativeGetMonitorCount
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_fastscreen_FastScreen_nativeGetMonitorCount(JNIEnv *, jobject);

#ifdef __cplusplus
}
#endif

#endif /* FASTSCREEN_H */
