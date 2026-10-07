/* Relocate production WebKitGTK's private helpers without disabling its sandbox. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WK_PREFIX "/usr/lib/x86_64-linux-gnu/webkitgtk-6.0/"

static int paths(char *root, char *libs, char *helpers) {
    Dl_info info;
    if (!dladdr((void *)paths, &info) || !info.dli_fname) return 0;
    char location[PATH_MAX];
    if (!realpath(info.dli_fname, location)) return 0;
    char *slash = strrchr(location, '/');
    if (!slash) return 0;
    *slash = 0;
    if (snprintf(libs, PATH_MAX, "%s", location) >= PATH_MAX) return 0;
    slash = strrchr(location, '/');
    if (!slash) return 0;
    *slash = 0;
    return snprintf(root, PATH_MAX, "%s", location) < PATH_MAX
        && snprintf(helpers, PATH_MAX, "%s%s", location, WK_PREFIX) < PATH_MAX;
}

static const char *relocate(const char *name, char *mapped) {
    if (!name) return name;
    if (!strcmp(name, "/usr/bin/xdg-dbus-proxy")) {
        char root[PATH_MAX], libs[PATH_MAX], helpers[PATH_MAX];
        if (paths(root, libs, helpers)
                && snprintf(mapped, PATH_MAX, "%s%s", root, name) < PATH_MAX)
            return mapped;
        return name;
    }
    if (strncmp(name, WK_PREFIX, strlen(WK_PREFIX))) return name;
    char root[PATH_MAX], libs[PATH_MAX], helpers[PATH_MAX];
    if (!paths(root, libs, helpers)) return name;
    if (snprintf(mapped, PATH_MAX, "%s%s", helpers, name + strlen(WK_PREFIX)) >= PATH_MAX)
        return name;
    return mapped;
}

/* Bubblewrap also binds the proxy's compiled-in path from its sealed
   argument file. Replace that path in a fresh descriptor; retain every
   namespace, permission, seccomp and IPC argument supplied by WebKit. */
static int proxy_arguments(void *launcher, const char *descriptor, const char *proxy) {
    void (*take_fd)(void *, int, int) = dlsym(RTLD_NEXT, "g_subprocess_launcher_take_fd");
    char *end;
    long source = strtol(descriptor, &end, 10);
    struct stat status;
    if (!take_fd || *end || source < 0 || source > INT_MAX
            || fstat((int)source, &status) || status.st_size <= 0
            || status.st_size > 1024 * 1024) return -1;
    size_t size = (size_t)status.st_size;
    char *args = malloc(size);
    if (!args) return -1;
    if (pread((int)source, args, size, 0) != (ssize_t)size || args[size - 1]) {
        free(args);
        return -1;
    }
    int fd = memfd_create("deltagram-webkit-sandbox", MFD_CLOEXEC);
    if (fd < 0) { free(args); return -1; }
    for (size_t offset = 0; offset < size;) {
        const char *arg = args + offset;
        size_t length = strlen(arg) + 1;
        const char *value = !strcmp(arg, "/usr/bin/xdg-dbus-proxy") ? proxy : arg;
        size_t bytes = strlen(value) + 1;
        if (write(fd, value, bytes) != (ssize_t)bytes) {
            close(fd);
            free(args);
            return -1;
        }
        offset += length;
    }
    free(args);
    if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
    take_fd(launcher, fd, fd);
    return fd;
}

void *g_subprocess_launcher_spawnv(void *launcher, const char *const *argv, void **error) {
    void *(*original)(void *, const char *const *, void **) =
        dlsym(RTLD_NEXT, "g_subprocess_launcher_spawnv");
    if (!original || !argv || !argv[0]) return NULL;
    size_t count = 0;
    while (argv[count]) ++count;
    char mapped[PATH_MAX];
    const char *path = relocate(argv[0], mapped);
    if (path != argv[0]) {
        const char **next = calloc(count + 1, sizeof(*next));
        if (!next) return original(launcher, argv, error);
        memcpy(next, argv, count * sizeof(*next));
        next[0] = path;
        void *result = original(launcher, next, error);
        free(next);
        return result;
    }
    /* WebKit's bubblewrap argv is: bwrap --args FD -- helper ...
       Retain its namespaces, seccomp and IPC descriptors; expose only the
       read-only bundle and launch the relocated helper inside that sandbox. */
    if (count >= 5 && !strcmp(argv[0], "/usr/bin/bwrap")
            && !strcmp(argv[1], "--args") && !strcmp(argv[3], "--")
            && relocate(argv[4], mapped) != argv[4]) {
        char root[PATH_MAX], libs[PATH_MAX], helpers[PATH_MAX], loader[PATH_MAX], bwrap[PATH_MAX], preload[PATH_MAX], executable[PATH_MAX];
        if (!paths(root, libs, helpers)
                || snprintf(loader, sizeof loader, "%s/ld-linux-x86-64.so.2.real", libs) >= PATH_MAX
                || snprintf(bwrap, sizeof bwrap, "%s/usr/bin/bwrap", root) >= PATH_MAX
                || snprintf(preload, sizeof preload, "%s/libwebkitfix.so", libs) >= PATH_MAX
                || snprintf(executable, sizeof executable, "%s.real", mapped) >= PATH_MAX)
            return original(launcher, argv, error);
        char descriptor[32];
        const char *arguments = argv[2];
        if (!strcmp(argv[4], "/usr/bin/xdg-dbus-proxy")) {
            int fd = proxy_arguments(launcher, argv[2], executable);
            if (fd < 0) return original(launcher, argv, error);
            snprintf(descriptor, sizeof descriptor, "%d", fd);
            arguments = descriptor;
        }
        const char **next = calloc(count + 16, sizeof(*next));
        if (!next) return original(launcher, argv, error);
        size_t index = 0;
        if (access(argv[0], X_OK)) {
            next[index++] = loader;
            next[index++] = "--library-path";
            next[index++] = libs;
            next[index++] = bwrap;
        } else {
            next[index++] = argv[0];
        }
        next[index++] = argv[1];
        next[index++] = arguments;
        next[index++] = "--ro-bind";
        next[index++] = root;
        next[index++] = root;
        next[index++] = "--";
        /* The sandbox deliberately has no /bin/sh: bypass the outer helper
           script and use the bundled ELF loader directly. */
        next[index++] = loader;
        next[index++] = "--library-path";
        next[index++] = libs;
        next[index++] = "--preload";
        next[index++] = preload;
        next[index++] = "--argv0";
        next[index++] = mapped;
        next[index++] = executable;
        for (size_t i = 5; i < count; ++i) next[index++] = argv[i];
        void *result = original(launcher, next, error);
        free(next);
        return result;
    }
    return original(launcher, argv, error);
}

void *g_module_open(const char *file, int flags) {
    void *(*original)(const char *, int) = dlsym(RTLD_NEXT, "g_module_open");
    char mapped[PATH_MAX];
    return original ? original(relocate(file, mapped), flags) : NULL;
}

void *g_module_open_full(const char *file, int flags, void **error) {
    void *(*original)(const char *, int, void **) = dlsym(RTLD_NEXT, "g_module_open_full");
    char mapped[PATH_MAX];
    return original ? original(relocate(file, mapped), flags, error) : NULL;
}
