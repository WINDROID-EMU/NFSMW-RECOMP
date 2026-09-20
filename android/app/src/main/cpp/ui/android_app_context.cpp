#include "android_app_context.h"
#include <android/log.h>

#define TAG "NFS-AndroidContext"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace rex::ui {

AndroidWindowedAppContext::AndroidWindowedAppContext(struct android_app* app)
    : WindowedAppContext(), app_(app) {
  LOGI("AndroidWindowedAppContext created on thread %lu", (unsigned long)pthread_self());
}

AndroidWindowedAppContext::~AndroidWindowedAppContext() {
  LOGI("AndroidWindowedAppContext destroyed");
}

bool AndroidWindowedAppContext::Initialize() {
  if (!app_) {
    LOGE("Cannot initialize AndroidWindowedAppContext with null android_app");
    return false;
  }
  return true;
}

void AndroidWindowedAppContext::SetNativeWindow(ANativeWindow* window) {
  native_window_ = window;
}

void AndroidWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  if (app_ && app_->looper) {
    ALooper_wake(app_->looper);
  }
}

void AndroidWindowedAppContext::PlatformQuitFromUIThread() {
  LOGI("PlatformQuitFromUIThread requested");
  quit_requested_ = true;
  if (app_ && app_->activity) {
    ANativeActivity_finish(app_->activity);
  }
}

bool AndroidWindowedAppContext::PumpEvents(int timeout_millis) {
  if (quit_requested_ || (app_ && app_->destroyRequested)) {
    return false;
  }

  int events = 0;
  struct android_poll_source* source = nullptr;

  while (ALooper_pollOnce(timeout_millis, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
    if (source != nullptr) {
      source->process(app_, source);
    }

    if (app_->destroyRequested) {
      quit_requested_ = true;
      break;
    }

    // Execute functions enqueued into the UI thread.
    ExecutePendingFunctionsFromUIThread();
  }

  ExecutePendingFunctionsFromUIThread();
  return !quit_requested_ && !app_->destroyRequested && !HasQuitFromUIThread();
}

}  // namespace rex::ui
