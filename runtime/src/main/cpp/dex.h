#pragma once

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true when the entrypoint class was loaded and invoked.
// Returns false (with any pending JNI exception cleared) when loading
// failed: the target process keeps running, just without the module injected.
bool dex_load_and_invoke(
        JNIEnv *env,
        const char *package_name,
        const void *dex_block, uint32_t dex_length,
        const void *properties_block, uint32_t properties_length,
        bool use_binder_interceptors
);

#ifdef __cplusplus
};
#endif
