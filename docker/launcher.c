#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef DELTA_TEL_RUNTIME_ID
#error Runtime payload digest is required
#endif
static const char key[] = "v2-" DELTA_TEL_RUNTIME_ID;
static volatile sig_atomic_t child_pid;
static void forward(int signal) { if (child_pid > 0) kill(child_pid, signal); }
static void fail(const char *message) { perror(message); exit(1); }
static void path(char *out, size_t size, const char *parent, const char *name) {
    if (snprintf(out, size, "%s/%s", parent, name) >= (int)size) {
        errno = ENAMETOOLONG; fail("runtime path");
    }
}
static void directories(const char *name) {
    char copy[4096];
    if (strlen(name) >= sizeof copy) { errno = ENAMETOOLONG; fail("cache path"); }
    strcpy(copy, name);
    for (char *p = copy + 1; ; ++p) {
        if (*p != '/' && *p) continue;
        char saved = *p; *p = 0;
        if (mkdir(copy, 0700) && errno != EEXIST) fail("create cache directory");
        *p = saved; if (!saved) break;
    }
}
static void private_directory(const char *name) {
    directories(name);
    struct stat st;
    if (lstat(name, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
        errno = EACCES; fail("unsafe runtime cache directory");
    }
    if (chmod(name, 0700)) fail("protect runtime cache");
}
static int remove_entry(const char *name, const struct stat *st, int type, struct FTW *walk) {
    (void)st; (void)type; (void)walk; return remove(name);
}
static void remove_tree(const char *name) {
    nftw(name, remove_entry, 32, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
}
static int ready(const char *root) {
    char name[4096], digest[sizeof key]; struct stat st;
    if (lstat(root, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return 0;
    path(name, sizeof name, root, ".runtime-ready");
    int fd = open(name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    int valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == getuid()
        && st.st_size == sizeof key - 1
        && read(fd, digest, sizeof key) == sizeof key - 1
        && !memcmp(digest, key, sizeof key - 1);
    close(fd);
    path(name, sizeof name, root, "AppRun");
    return valid && !lstat(name, &st) && S_ISREG(st.st_mode)
        && st.st_uid == getuid() && !access(name, X_OK);
}
static int cache_lock(const char *root, const char *id) {
    char name[4096], leaf[96]; struct stat st;
    snprintf(leaf, sizeof leaf, ".%s.lock", id);
    path(name, sizeof name, root, leaf);
    int fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid())
        fail("runtime cache lock");
    return fd;
}
static int wait_child(void) {
    int status;
    while (waitpid(child_pid, &status, 0) < 0) if (errno != EINTR) fail("wait");
    child_pid = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
static void prune(const char *root) {
    DIR *dir = opendir(root); if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *id = entry->d_name;
        if (strlen(id) != sizeof key - 1 || strncmp(id, "v2-", 3) || !strcmp(id, key)) continue;
        if (strspn(id + 3, "0123456789abcdef") != 64) continue;
        int lock = cache_lock(root, id);
        if (!flock(lock, LOCK_EX | LOCK_NB)) {
            char name[4096]; path(name, sizeof name, root, id);
            remove_tree(name);
        }
        close(lock);
    }
    closedir(dir);
}
static void extract(int source, off_t size, uint64_t length, const char *root, const char *final) {
    char temporary[4096], image[4096], extracted[4096], marker[4096];
    char prefix[96]; snprintf(prefix, sizeof prefix, ".%s.extract-", key);
    DIR *stale = opendir(root);
    if (stale) {
        struct dirent *entry;
        while ((entry = readdir(stale))) if (!strncmp(entry->d_name, prefix, strlen(prefix))) {
            char name[4096]; path(name, sizeof name, root, entry->d_name); remove_tree(name);
        }
        closedir(stale);
    }
    char pattern[104]; snprintf(pattern, sizeof pattern, "%sXXXXXX", prefix);
    path(temporary, sizeof temporary, root, pattern);
    if (!mkdtemp(temporary)) fail("temporary runtime directory");
    path(image, sizeof image, temporary, "runtime.AppImage");
    int target = open(image, O_WRONLY | O_CREAT | O_EXCL, 0700);
    if (target < 0) fail("create runtime");
    if (lseek(source, size - 8 - length, SEEK_SET) < 0) fail("seek payload");
    char buffer[131072];
    while (length) {
        size_t amount = length < sizeof buffer ? (size_t)length : sizeof buffer;
        ssize_t got = read(source, buffer, amount);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) fail("read runtime");
        for (ssize_t written = 0; written < got;) {
            ssize_t count = write(target, buffer + written, got - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("write runtime");
            written += count;
        }
        length -= got;
    }
    close(target);
    pid_t parent = getpid();
    child_pid = fork(); if (child_pid < 0) fail("fork extractor");
    if (!child_pid) {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) _exit(1);
        if (chdir(temporary)) fail("extract directory");
        int quiet = open("/dev/null", O_WRONLY);
        if (quiet < 0 || dup2(quiet, STDOUT_FILENO) < 0) fail("extract output");
        close(quiet);
        unsetenv("APPIMAGE_EXTRACT_AND_RUN");
        execl(image, image, "--appimage-extract", (char *)NULL); fail("extract runtime");
    }
    int status = wait_child();
    path(extracted, sizeof extracted, temporary, "squashfs-root");
    path(marker, sizeof marker, extracted, ".runtime-ready");
    target = status ? -1 : open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (target < 0 || write(target, key, sizeof key - 1) != sizeof key - 1) {
        if (target >= 0) close(target);
        remove_tree(temporary);
        fprintf(stderr, "Could not extract the portable runtime.\n"); exit(1);
    }
    close(target);
    if (!ready(extracted)) {
        remove_tree(temporary); fprintf(stderr, "Incomplete portable runtime.\n"); exit(1);
    }
    if (rename(extracted, final)) fail("publish runtime cache");
    unlink(image); rmdir(temporary);
}
int main(int argc, char **argv) {
    (void)argc;
    char self[4096], base[4096], app[4096], root[4096], final[4096], executable[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n < 0 || n >= (ssize_t)sizeof self - 1) fail("executable path");
    self[n] = 0;
    int source = open(self, O_RDONLY | O_CLOEXEC); if (source < 0) fail("open executable");
    off_t size = lseek(source, 0, SEEK_END); unsigned char trailer[8];
    if (size < 16 || pread(source, trailer, 8, size - 8) != 8) fail("read payload");
    uint64_t length = 0;
    for (int i = 7; i >= 0; --i) length = (length << 8) | trailer[i];
    if (!length || length > (uint64_t)size - 8) { fprintf(stderr, "Invalid payload\n"); return 1; }
    const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (cache && cache[0] == '/') snprintf(base, sizeof base, "%s", cache);
    else if (home && home[0] == '/') path(base, sizeof base, home, ".cache");
    else snprintf(base, sizeof base, "/tmp/deltagram-%lu", (unsigned long)getuid());
    path(app, sizeof app, base, "deltagram"); private_directory(app);
    path(root, sizeof root, app, "runtime"); private_directory(root);
    path(final, sizeof final, root, key);
    int lock = cache_lock(root, key);
    signal(SIGINT, forward); signal(SIGTERM, forward); signal(SIGHUP, forward);
    for (;;) {
        if (flock(lock, LOCK_SH)) fail("lock cached runtime");
        if (ready(final)) break;
        if (flock(lock, LOCK_UN)) fail("unlock runtime cache");
        if (!flock(lock, LOCK_EX | LOCK_NB)) {
            if (!ready(final)) { remove_tree(final); extract(source, size, length, root, final); }
            if (flock(lock, LOCK_SH)) fail("retain runtime cache");
            break;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) fail("lock runtime extraction");
        usleep(10000);
    }
    close(source); prune(root);
    path(executable, sizeof executable, final, "AppRun");
    setenv("DELTA_TEL_EXECUTABLE", self, 1);
    child_pid = fork(); if (child_pid < 0) fail("fork application");
    if (!child_pid) { argv[0] = executable; execv(executable, argv); fail("start application"); }
    int status = wait_child(); close(lock); return status;
}
