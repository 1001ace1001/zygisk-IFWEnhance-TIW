#include "main.h"

#include "logger.h"

#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct Resource *resource_map_fd(int fd) {
    if (fd < 0) {
        LOGW("resource_map_fd: invalid fd");
        return NULL;
    }

    struct stat s;
    if (fstat(fd, &s) < 0) {
        LOGW("resource_map_fd: fstat failed");
        return NULL;
    }

    if (s.st_size <= 0) {
        LOGW("resource_map_fd: empty file");
        return NULL;
    }

    void *base = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        LOGW("resource_map_fd: mmap failed");
        return NULL;
    }

    struct Resource *resource = (struct Resource *) malloc(sizeof(struct Resource));
    if (resource == NULL) {
        munmap(base, s.st_size);
        return NULL;
    }

    resource->base = base;
    resource->length = s.st_size;

    return resource;
}

struct Resource *resource_map_file(const char *path) {
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        LOGW("resource_map_file: open %s failed", path);
        return NULL;
    }

    struct Resource *resource = resource_map_fd(fd);

    close(fd);

    return resource;
}

void resource_release(struct Resource *resource) {
    if (resource == NULL) {
        return;
    }

    munmap(resource->base, resource->length);

    free(resource);
}
