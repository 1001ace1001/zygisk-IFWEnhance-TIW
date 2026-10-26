#include "process.h"

#include "logger.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

void process_get_package_name(JNIEnv *env, jstring process_name, char **package_name) {
    *package_name = NULL;

    if (process_name == NULL) {
        return;
    }

    const char *str = (*env)->GetStringUTFChars(env, process_name, NULL);

    // GetStringUTFChars fails (returns NULL with a pending exception) on OOM;
    // this MUST NOT crash the freshly forked process.
    if (str == NULL) {
        LOGW("process_get_package_name: GetStringUTFChars failed");

        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }

        return;
    }

    *package_name = strdup(str);

    (*env)->ReleaseStringUTFChars(env, process_name, str);

    if (*package_name == NULL) {
        return;
    }

    char *split = strchr(*package_name, ':');
    if (split != NULL) {
        *split = '\0';
    }
}
