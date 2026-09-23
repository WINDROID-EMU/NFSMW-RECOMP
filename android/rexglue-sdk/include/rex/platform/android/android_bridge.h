/**
 * @file        platform/android/android_bridge.h
 * @brief       Direct NDK / JNI Bridge for Android application lifecycle & hardware I/O
 *
 * @copyright   Copyright (c) 2026 ReXGlue Authors
 * @license     BSD 3-Clause License
 */

#pragma once

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <rex/ui/window_android.h>
#include <rex/input/android/android_input_driver.h>

namespace rex::platform::android {

class AndroidBridge {
 public:
  static void SetActiveWindow(rex::ui::WindowAndroid* window);
  static rex::ui::WindowAndroid* GetActiveWindow();

  static void OnSurfaceCreated(ANativeWindow* native_window);
  static void OnSurfaceDestroyed();
  static void OnSurfaceChanged(ANativeWindow* native_window, int width, int height);

  // Direct input forwarding
  static void DispatchKeyEvent(int action, int key_code);
  static void DispatchMotionEvent(int axis, float value);
  static void DispatchVirtualButton(uint16_t button_mask, bool pressed);
  static void DispatchVirtualStick(bool is_right, int16_t x, int16_t y);
  static void DispatchVirtualTrigger(bool is_right, uint8_t value);
};

}  // namespace rex::platform::android

#ifdef __cplusplus
extern "C" {
#endif

// JNI exports for Android frontend integration
JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_setSurface(
    JNIEnv* env, jclass clazz, jobject surface);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_destroySurface(
    JNIEnv* env, jclass clazz);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_dispatchKeyEvent(
    JNIEnv* env, jclass clazz, jint action, jint key_code);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_dispatchMotionEvent(
    JNIEnv* env, jclass clazz, jint axis, jfloat value);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_setVirtualButton(
    JNIEnv* env, jclass clazz, jint button_mask, jboolean pressed);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_setVirtualStick(
    JNIEnv* env, jclass clazz, jboolean is_right, jshort x, jshort y);

JNIEXPORT void JNICALL Java_com_rexglue_runtime_NativeBridge_setVirtualTrigger(
    JNIEnv* env, jclass clazz, jboolean is_right, jint value);

#ifdef __cplusplus
}
#endif
