#include "dex.h"

#include "binder.h"
#include "logger.h"

#include <stddef.h>
#include <dlfcn.h>
#include <string.h>

// This runs in the freshly specialized target process (including
// system_server). Any failure here must degrade to "module not loaded",
// NEVER abort(): aborting kills the forked app (or system_server, which
// soft-reboots the whole system).

static jclass find_class(JNIEnv *env, const char *name) {
    jclass clazz = (*env)->FindClass(env, name);

    if (clazz == NULL) {
        LOGW("dex: FindClass %s failed", name);

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }

    return clazz;
}

static jmethodID get_static_method(JNIEnv *env, jclass clazz, const char *name, const char *signature) {
    jmethodID method = (*env)->GetStaticMethodID(env, clazz, name, signature);

    if (method == NULL) {
        LOGW("dex: GetStaticMethodID %s failed", name);

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }

    return method;
}

static jmethodID get_method(JNIEnv *env, jclass clazz, const char *name, const char *signature) {
    jmethodID method = (*env)->GetMethodID(env, clazz, name, signature);

    if (method == NULL) {
        LOGW("dex: GetMethodID %s failed", name);

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    }

    return method;
}

bool dex_load_and_invoke(
        JNIEnv *env,
        const char *package_name,
        const void *dex_block, uint32_t dex_length,
        const void *properties_block, uint32_t properties_length,
        bool use_binder_interceptors
) {
    if (dex_block == NULL || properties_block == NULL) {
        LOGW("dex: null resources");

        return false;
    }

    jclass c_class_loader = find_class(env, "java/lang/ClassLoader");
    if (c_class_loader == NULL) {
        return false;
    }

    jmethodID m_get_system_class_loader = get_static_method(
            env,
            c_class_loader,
            "getSystemClassLoader",
            "()Ljava/lang/ClassLoader;"
    );
    if (m_get_system_class_loader == NULL) {
        return false;
    }

    jobject o_system_class_loader = (*env)->CallStaticObjectMethod(
            env,
            c_class_loader,
            m_get_system_class_loader
    );
    if (o_system_class_loader == NULL) {
        LOGW("dex: getSystemClassLoader failed");

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }

        return false;
    }

    jclass c_dex_class_loader = find_class(env, "dalvik/system/InMemoryDexClassLoader");
    if (c_dex_class_loader == NULL) {
        return false;
    }

    jmethodID m_dex_class_loader = get_method(
            env,
            c_dex_class_loader,
            "<init>",
            "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V"
    );
    if (m_dex_class_loader == NULL) {
        return false;
    }

    jobject o_dex_class_loader = (*env)->NewObject(
            env,
            c_dex_class_loader,
            m_dex_class_loader,
            (*env)->NewDirectByteBuffer(env, (void *) dex_block, dex_length),
            o_system_class_loader
    );
    if (o_dex_class_loader == NULL) {
        LOGW("dex: create InMemoryDexClassLoader failed");

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }

        return false;
    }

    jmethodID m_load_class = get_method(
            env,
            c_class_loader,
            "loadClass",
            "(Ljava/lang/String;)Ljava/lang/Class;"
    );
    if (m_load_class == NULL) {
        return false;
    }

    jclass c_loader = (*env)->CallObjectMethod(
            env,
            o_dex_class_loader,
            m_load_class,
            (*env)->NewStringUTF(env, "com.github.kr328.zloader.internal.Loader")
    );
    if (c_loader == NULL) {
        LOGW("dex: load Loader class failed");

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }

        return false;
    }

    if (use_binder_interceptors) {
        LOGD("Enable binder interceptors");

        // Degrade instead of aborting when the ART internal symbol is
        // missing (e.g. after an ART update): the module simply runs
        // without binder interception in that process.
        if (binder_interceptors_init(env, c_loader) != 0) {
            LOGW("dex: binder interceptors unavailable, continue without them");

            use_binder_interceptors = false;
        }
    }

    jmethodID m_load = get_static_method(env, c_loader, "load", "(Ljava/lang/String;Ljava/nio/ByteBuffer;Z)V");
    if (m_load == NULL) {
        return false;
    }

    // Loader.load(...) catches everything on the Java side; nothing is
    // expected to be pending afterwards, but check defensively anyway.
    (*env)->CallStaticVoidMethod(
            env,
            c_loader,
            m_load,
            (*env)->NewStringUTF(env, package_name),
            (*env)->NewDirectByteBuffer(env, (void *) properties_block, properties_length),
            (jboolean) use_binder_interceptors
    );

    if ((*env)->ExceptionCheck(env)) {
        LOGW("dex: Loader.load threw");

        (*env)->ExceptionClear(env);

        return false;
    }

    return true;
}
