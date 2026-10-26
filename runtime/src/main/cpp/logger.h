#pragma once

#include <android/log.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAG "ZygoteLoader[Native]"

#ifdef DEBUG
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#else
#define LOGD(...)
#endif

// Warnings and errors are always logged, even in release builds:
// they mark recoverable failures that would otherwise be invisible.
// INFO is always-on too: it marks one-shot lifecycle events (module
// initialize, system_server injection decisions) that must be visible
// in production logcat for post-mortem diagnosis of bugreports.
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Fatal errors are ALWAYS logged before aborting, otherwise tombstones
// are produced without any abort message and are impossible to diagnose.
#define LOGF(...) do { __android_log_print(ANDROID_LOG_FATAL, TAG, __VA_ARGS__); abort(); } while (0)

#define fatal_assert(expr) if (!(expr)) LOGF("!(" #expr ")")

#ifdef __cplusplus
};
#endif
