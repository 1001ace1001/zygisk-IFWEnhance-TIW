#include "main_zygisk.h"

#include "logger.h"
#include "serializer.h"
#include "properties.h"
#include "process.h"
#include "dex.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <jni.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

// ============================================================
// Crash-safety rules for this file
//
// Zygisk runs this code in three distinct contexts:
//
//   1. the zygote / freshly forked (not yet specialized) child,
//      during onLoad/pre*Specialize;
//   2. the freshly specialized target process (including
//      system_server), during post*Specialize;
//   3. the module's root companion process (zygiskd64), for
//      handleFileRequest().
//
// abort() in context (1) kills the app being forked (SIGABRT,
// "failed to start"), abort() in context (2) can kill
// system_server (soft reboot), and abort() in context (3) kills
// the companion daemon, which in turn makes the next fork abort
// in context (1) - a cascade observed in production tombstones.
//
// Therefore: NO fatal_assert() on socket I/O, open(), mmap() or
// any other runtime failure in this file. Every failure degrades
// to "module not injected into this process" and is logged with
// LOGW/LOGE instead.
// ============================================================

enum FileCommand : int {
    INITIALIZE, IS_INITIALIZED, IS_USE_BINDER_INTERCEPTORS, GET_RESOURCES, SHOULD_ENABLE_FOR_PACKAGE
};

struct InitializeData {
    char dataDirectory[PATH_MAX] = {0};
    char moduleId[128] = {0};
    bool useBinderInterceptors = false;
};

static void extractInitializeData(InitializeData *data, const char *key, const char *value) {
    if (strcmp(key, "dataDirectory") == 0) {
        strncpy(data->dataDirectory, value, sizeof(data->dataDirectory) - 1);
        data->dataDirectory[sizeof(data->dataDirectory) - 1] = '\0';
    } else if (strcmp(key, "id") == 0) {
        strncpy(data->moduleId, value, sizeof(data->moduleId) - 1);
        data->moduleId[sizeof(data->moduleId) - 1] = '\0';
    } else if (strcmp(key, "useBinderInterceptors") == 0) {
        data->useBinderInterceptors = (strcmp(value, "true") == 0);
    }
}

// mkdir -p for absolute paths. Returns 0 on success (including EEXIST).
static int mkdir_p(const char *path, mode_t mode) {
    char buffer[PATH_MAX] = {0};
    strncpy(buffer, path, sizeof(buffer) - 1);

    for (char *p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

// Reads /data/adb/zloader/data-root-directory, creating it on first use.
// This mirrors customize.d/40-initialize-data-directory.sh so the runtime
// can recover when module.prop lacks "dataDirectory" (an install whose
// customize.sh never ran, e.g. interrupted by a framework crash, or a
// third-party installer that skips install scripts).
// Companion context only (runs as root). Returns 0 on success.
static int resolve_data_root_directory(char *out, size_t outSize) {
    static const char *markerFile = "/data/adb/zloader/data-root-directory";
    static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    int fd = open(markerFile, O_RDONLY);
    if (fd >= 0) {
        ssize_t length = read(fd, out, outSize - 1);
        close(fd);
        if (length > 0) {
            out[length] = '\0';
            while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == '\r' ||
                                  out[length - 1] == ' ')) {
                out[--length] = '\0';
            }
            if (length > 0) {
                return 0;
            }
        }
    }

    // First use: generate a random suffix and persist the marker exactly
    // like the install script does.
    char suffix[17] = "const";

    int urandom = open("/dev/urandom", O_RDONLY);
    if (urandom >= 0) {
        unsigned char raw[16] = {0};
        if (read(urandom, raw, sizeof(raw)) == (ssize_t) sizeof(raw)) {
            for (int i = 0; i < 16; i++) {
                suffix[i] = alphabet[raw[i] % 62];
            }
        }
        close(urandom);
    }

    if (mkdir_p("/data/adb/zloader", 0755) != 0) {
        LOGW("Companion: failed to create /data/adb/zloader");

        return -1;
    }

    char rootPath[PATH_MAX] = {0};
    snprintf(rootPath, sizeof(rootPath), "/data/system/zloader-%s", suffix);

    int marker = open(markerFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (marker < 0) {
        LOGW("Companion: failed to create %s", markerFile);

        return -1;
    }

    if (write(marker, rootPath, strlen(rootPath)) < 0) {
        LOGW("Companion: failed to write %s", markerFile);

        close(marker);

        return -1;
    }

    close(marker);

    snprintf(out, outSize, "%s", rootPath);

    return 0;
}

static void handleFileRequest(int client) {
    static pthread_mutex_t initializeLock = PTHREAD_MUTEX_INITIALIZER;
    static int moduleProp = -1;
    static int classesDex = -1;
    static int moduleDirectory = -1;
    static int dataDirectory = -1;
    static bool useBinderInterceptors = false;

    int command = -1;
    if (serializer_read_int(client, &command) <= 0) {
        // The peer (a forked process blocked in pre*Specialize) died or the
        // connection broke. This is expected and recoverable: dropping the
        // request only disables the module for that single process.
        LOGW("Companion: failed to read command (peer closed connection?)");

        return;
    }

    switch (static_cast<FileCommand>(command)) {
        case INITIALIZE: {
            pthread_mutex_lock(&initializeLock);

            if (moduleDirectory == -1) {
                LOGD("Remote initializing");

                int receivedDir = -1;
                int prop = -1;
                int dex = -1;
                int data = -1;

                do {
                    if (serializer_read_file_descriptor(client, &receivedDir) <= 0) {
                        LOGE("Companion: INITIALIZE: failed to receive module dir fd");

                        break;
                    }

                    prop = openat(receivedDir, "module.prop", O_RDONLY);
                    if (prop < 0) {
                        LOGE("Companion: INITIALIZE: open module.prop failed");

                        break;
                    }

                    dex = openat(receivedDir, "classes.dex", O_RDONLY);
                    if (dex < 0) {
                        LOGE("Companion: INITIALIZE: open classes.dex failed");

                        break;
                    }

                    InitializeData initializeData;
                    struct Resource *block = resource_map_fd(prop);
                    if (block == nullptr) {
                        LOGE("Companion: INITIALIZE: failed to map module.prop");

                        break;
                    }

                    properties_for_each(
                            block->base, block->length,
                            &initializeData,
                            reinterpret_cast<properties_for_each_block>(&extractInitializeData)
                    );

                    resource_release(block);

                    bool healed = false;

                    if (strlen(initializeData.dataDirectory) == 0) {
                        LOGW("Companion: INITIALIZE: dataDirectory missing in module.prop, self-healing");

                        char dataRoot[PATH_MAX] = {0};
                        if (resolve_data_root_directory(dataRoot, sizeof(dataRoot)) != 0) {
                            LOGE("Companion: INITIALIZE: self-heal failed to resolve data root directory");

                            break;
                        }

                        const char *moduleId = strlen(initializeData.moduleId) > 0
                                                       ? initializeData.moduleId
                                                       : "module";

                        snprintf(initializeData.dataDirectory, sizeof(initializeData.dataDirectory),
                                 "%s/%s", dataRoot, moduleId);

                        healed = true;
                    }

                    // (Re)create the directory if it does not exist yet: it may
                    // have been wiped from /data/system, or a self-healed path
                    // never went through 40-initialize-data-directory.sh.
                    if (mkdir_p(initializeData.dataDirectory, 0700) != 0) {
                        LOGE("Companion: INITIALIZE: mkdir %s failed", initializeData.dataDirectory);

                        break;
                    }

                    // Best-effort ownership/permissions, mirroring
                    // customize.d/90-restore-module-permission.sh.
                    chown(initializeData.dataDirectory, 1000, 1000);
                    chmod(initializeData.dataDirectory, 0700);

                    data = open(initializeData.dataDirectory, O_RDONLY | O_DIRECTORY);
                    if (data < 0) {
                        LOGE("Companion: INITIALIZE: open dataDirectory %s failed",
                             initializeData.dataDirectory);

                        break;
                    }

                    // Persist a healed dataDirectory back into module.prop so
                    // the manager UI shows it and later zygote generations do
                    // not depend on the self-heal path.
                    if (healed) {
                        int propAppend = openat(receivedDir, "module.prop", O_WRONLY | O_APPEND);
                        if (propAppend >= 0) {
                            char line[PATH_MAX + 32] = {0};
                            snprintf(line, sizeof(line), "dataDirectory=%s\n",
                                     initializeData.dataDirectory);
                            // Best-effort only: on failure the self-heal runs again.
                            if (write(propAppend, line, strlen(line)) < 0) {
                                LOGW("Companion: INITIALIZE: failed to persist dataDirectory");
                            }
                            close(propAppend);
                        } else {
                            LOGW("Companion: INITIALIZE: module.prop not writable (self-heal not persisted)");
                        }
                    }

                    // commit only when everything succeeded
                    moduleDirectory = receivedDir;
                    moduleProp = prop;
                    classesDex = dex;
                    dataDirectory = data;
                    useBinderInterceptors = initializeData.useBinderInterceptors;

                    receivedDir = -1;
                    prop = -1;
                    dex = -1;
                    data = -1;

                    LOGI("Companion: initialized (dataDirectory=%s)", initializeData.dataDirectory);

                    LOGD("Remote initialized: dataDirectory = %s", initializeData.dataDirectory);
                } while (false);

                if (moduleDirectory == -1) {
                    if (receivedDir >= 0) {
                        close(receivedDir);
                    }
                    if (prop >= 0) {
                        close(prop);
                    }
                    if (dex >= 0) {
                        close(dex);
                    }
                    if (data >= 0) {
                        close(data);
                    }
                }
            }

            if (serializer_write_int(client, moduleDirectory != -1 ? 1 : 0) <= 0) {
                LOGW("Companion: INITIALIZE: failed to reply");
            }

            pthread_mutex_unlock(&initializeLock);

            break;
        }
        case IS_INITIALIZED: {
            pthread_mutex_lock(&initializeLock);

            if (serializer_write_int(client, moduleDirectory != -1 ? 1 : 0) <= 0) {
                LOGW("Companion: IS_INITIALIZED: failed to reply");
            }

            pthread_mutex_unlock(&initializeLock);

            break;
        }
        case IS_USE_BINDER_INTERCEPTORS: {
            if (serializer_write_int(client, useBinderInterceptors ? 1 : 0) <= 0) {
                LOGW("Companion: IS_USE_BINDER_INTERCEPTORS: failed to reply");
            }

            break;
        }
        case GET_RESOURCES: {
            pthread_mutex_lock(&initializeLock);

            if (moduleProp == -1 || classesDex == -1) {
                LOGW("Companion: GET_RESOURCES: not initialized");

                // Do not send anything: the client interprets the closed
                // connection as a failure and skips injection for this fork.
            } else if (serializer_write_file_descriptor(client, moduleProp) <= 0 ||
                       serializer_write_file_descriptor(client, classesDex) <= 0) {
                LOGW("Companion: GET_RESOURCES: failed to send fds");
            }

            pthread_mutex_unlock(&initializeLock);

            break;
        }
        case SHOULD_ENABLE_FOR_PACKAGE: {
            char *packageName = nullptr;

            if (serializer_read_string(client, &packageName) <= 0) {
                LOGW("Companion: SHOULD_ENABLE_FOR_PACKAGE: failed to read package name");

                break;
            }

            int enabled = 0;

            if (moduleDirectory != -1) {
                char path[PATH_MAX] = {0};
                snprintf(path, sizeof(path), "packages/%s", packageName);

                if (faccessat(moduleDirectory, path, F_OK, 0) == 0 ||
                    (dataDirectory != -1 && faccessat(dataDirectory, path, F_OK, 0) == 0)) {
                    enabled = 1;
                }
            } else {
                LOGW("Companion: SHOULD_ENABLE_FOR_PACKAGE: not initialized");
            }

            free(packageName);

            if (serializer_write_int(client, enabled) <= 0) {
                LOGW("Companion: SHOULD_ENABLE_FOR_PACKAGE: failed to reply");
            }

            break;
        }
        default: {
            LOGW("Companion: unknown command: %d", command);

            break;
        }
    }
}

void ZygoteLoaderModule::onLoad(zygisk::Api *_api, JNIEnv *_env) {
    api = _api;
    env = _env;

    if (!isInitialized()) {
        LOGD("Requesting initialize");

        initialize();
    }
}

void ZygoteLoaderModule::preAppSpecialize(zygisk::AppSpecializeArgs *args) {
    process_get_package_name(env, args->nice_name, &currentProcessName);

    prepareFork();
}

void ZygoteLoaderModule::postAppSpecialize(const zygisk::AppSpecializeArgs *args) {
    tryLoadDex();
}

void ZygoteLoaderModule::preServerSpecialize(zygisk::ServerSpecializeArgs *args) {
    currentProcessName = strdup(PACKAGE_NAME_SYSTEM_SERVER);

    prepareFork();
}

void ZygoteLoaderModule::postServerSpecialize(const zygisk::ServerSpecializeArgs *args) {
    tryLoadDex();
}

bool ZygoteLoaderModule::fetchResources() {
    int remote = api->connectCompanion();
    if (remote < 0) {
        LOGW("fetchResources: connectCompanion failed");

        return false;
    }

    if (serializer_write_int(remote, GET_RESOURCES) <= 0) {
        LOGW("fetchResources: failed to send GET_RESOURCES");

        close(remote);

        return false;
    }

    int modulePropFd = -1;
    int classesDexFd = -1;
    if (serializer_read_file_descriptor(remote, &modulePropFd) <= 0 ||
        serializer_read_file_descriptor(remote, &classesDexFd) <= 0) {
        LOGW("fetchResources: failed to receive resources");

        close(remote);

        return false;
    }

    close(remote);

    moduleProp = resource_map_fd(modulePropFd);
    classesDex = resource_map_fd(classesDexFd);

    close(modulePropFd);
    close(classesDexFd);

    if (moduleProp == nullptr || classesDex == nullptr) {
        LOGW("fetchResources: failed to map resources");

        if (moduleProp != nullptr) {
            resource_release(moduleProp);
            moduleProp = nullptr;
        }
        if (classesDex != nullptr) {
            resource_release(classesDex);
            classesDex = nullptr;
        }

        return false;
    }

    return true;
}

void ZygoteLoaderModule::reset() {
    useBinderInterceptors = false;

    free(currentProcessName);

    currentProcessName = nullptr;

    if (moduleProp != nullptr) {
        resource_release(moduleProp);
        moduleProp = nullptr;
    }
    if (classesDex != nullptr) {
        resource_release(classesDex);
        classesDex = nullptr;
    }
}

void ZygoteLoaderModule::prepareFork() {
    if (currentProcessName != nullptr && shouldEnableForPackage(currentProcessName)) {
        if (fetchResources()) {
            useBinderInterceptors = isUseBinderInterceptors();
        } else {
            // Companion is temporarily unavailable: skip injection for this
            // process instead of aborting (which would kill the forked app).
            LOGW("prepareFork: failed to fetch resources, skip injection");

            reset();
        }
    } else {
        reset();
    }

    if (!useBinderInterceptors) {
        LOGD("Request dlclose module library");

        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }
}

void ZygoteLoaderModule::tryLoadDex() {
    if (currentProcessName != nullptr && classesDex != nullptr && moduleProp != nullptr) {
        if (strcmp(currentProcessName, PACKAGE_NAME_SYSTEM_SERVER) == 0) {
            LOGI("Injecting dex into system_server");
        }

        LOGD("Loading in %s", currentProcessName);

        dex_load_and_invoke(
                env, currentProcessName,
                classesDex->base, classesDex->length,
                moduleProp->base, moduleProp->length,
                useBinderInterceptors
        );

        reset();
    }
}

bool ZygoteLoaderModule::shouldEnableForPackage(const char *packageName) {
    if (packageName == nullptr) {
        LOGW("shouldEnableForPackage: null package name");

        return false;
    }

    int remote = api->connectCompanion();
    if (remote < 0) {
        LOGW("shouldEnableForPackage: connectCompanion failed for %s", packageName);

        return false;
    }

    if (serializer_write_int(remote, SHOULD_ENABLE_FOR_PACKAGE) <= 0 ||
        serializer_write_string(remote, packageName) <= 0) {
        LOGW("shouldEnableForPackage: failed to send request for %s", packageName);

        close(remote);

        return false;
    }

    int r = 0;
    if (serializer_read_int(remote, &r) <= 0) {
        LOGW("shouldEnableForPackage: failed to read reply for %s", packageName);

        close(remote);

        return false;
    }

    LOGD("Enabled status for %s: %d", packageName, r);

    // The system_server decision is logged even in release builds: IFW
    // filtering silently stopping in system_server (e.g. after a framework
    // restart) is otherwise invisible in production bugreports.
    if (strcmp(packageName, PACKAGE_NAME_SYSTEM_SERVER) == 0) {
        if (r != 0) {
            LOGI("system_server injection allowed");
        } else {
            LOGI("system_server injection denied (packages/" PACKAGE_NAME_SYSTEM_SERVER
                 " missing from module and data directory)");
        }
    }

    close(remote);

    return r != 0;
}

bool ZygoteLoaderModule::initialize() {
    int remote = api->connectCompanion();
    if (remote < 0) {
        LOGW("initialize: connectCompanion failed");

        return false;
    }

    int moduleDir = api->getModuleDir();
    if (moduleDir < 0) {
        LOGW("initialize: getModuleDir failed");

        close(remote);

        return false;
    }

    if (serializer_write_int(remote, INITIALIZE) <= 0 ||
        serializer_write_file_descriptor(remote, moduleDir) <= 0) {
        LOGW("initialize: failed to send module dir");

        close(remote);

        return false;
    }

    int initialized = -1;
    if (serializer_read_int(remote, &initialized) <= 0) {
        LOGW("initialize: failed to read reply");

        close(remote);

        return false;
    }

    close(remote);

    if (initialized != 1) {
        LOGW("initialize: companion refused to initialize");

        return false;
    }

    return true;
}

bool ZygoteLoaderModule::isInitialized() {
    int remote = api->connectCompanion();
    if (remote < 0) {
        LOGW("isInitialized: connectCompanion failed");

        return false;
    }

    if (serializer_write_int(remote, IS_INITIALIZED) <= 0) {
        LOGW("isInitialized: failed to send request");

        close(remote);

        return false;
    }

    int initialized = -1;
    if (serializer_read_int(remote, &initialized) <= 0) {
        LOGW("isInitialized: failed to read reply");

        close(remote);

        return false;
    }

    close(remote);

    return initialized != 0;
}

bool ZygoteLoaderModule::isUseBinderInterceptors() {
    int remote = api->connectCompanion();
    if (remote < 0) {
        LOGW("isUseBinderInterceptors: connectCompanion failed");

        return false;
    }

    if (serializer_write_int(remote, IS_USE_BINDER_INTERCEPTORS) <= 0) {
        LOGW("isUseBinderInterceptors: failed to send request");

        close(remote);

        return false;
    }

    int enabled = 0;
    if (serializer_read_int(remote, &enabled) <= 0) {
        LOGW("isUseBinderInterceptors: failed to read reply");

        close(remote);

        return false;
    }

    close(remote);

    return enabled != 0;
}


REGISTER_ZYGISK_MODULE(ZygoteLoaderModule)

REGISTER_ZYGISK_COMPANION(handleFileRequest)
