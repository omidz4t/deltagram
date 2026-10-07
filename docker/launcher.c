#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t child_pid;
static void forward(int signal) { if (child_pid > 0) kill(child_pid, signal); }
static void fail(const char *message) { perror(message); exit(1); }

int main(int argc, char **argv) {
    char self[4096], temporary[4096], image[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0 || n >= (ssize_t)sizeof(self) - 1) fail("executable path");
    self[n] = 0;
    int source = open(self, O_RDONLY);
    if (source < 0) fail("open executable");
    off_t size = lseek(source, 0, SEEK_END);
    unsigned char trailer[8];
    if (size < 16 || pread(source, trailer, 8, size - 8) != 8) fail("read payload");
    uint64_t length = 0;
    for (int i = 7; i >= 0; --i) length = (length << 8) | trailer[i];
    if (!length || length > (uint64_t)size - 8) { fprintf(stderr, "Invalid payload\n"); return 1; }
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    if (snprintf(temporary, sizeof temporary, "%s/deltagram-XXXXXX", tmp) >= (int)sizeof temporary) fail("temporary path");
    if (!mkdtemp(temporary)) fail("temporary directory");
    snprintf(image, sizeof image, "%s/runtime.AppImage", temporary);
    int target = open(image, O_WRONLY | O_CREAT | O_EXCL, 0700);
    if (target < 0) fail("create runtime");
    if (lseek(source, size - 8 - length, SEEK_SET) < 0) fail("seek payload");
    char buffer[131072];
    while (length) {
        size_t amount = length < sizeof buffer ? (size_t)length : sizeof buffer;
        ssize_t got = read(source, buffer, amount);
        if (got <= 0) fail("read runtime");
        for (ssize_t written = 0; written < got;) {
            ssize_t count = write(target, buffer + written, got - written);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("write runtime");
            written += count;
        }
        length -= got;
    }
    close(source); close(target);
    setenv("APPIMAGE_EXTRACT_AND_RUN", "1", 1);
    setenv("DELTA_TEL_EXECUTABLE", self, 1);
    signal(SIGINT, forward); signal(SIGTERM, forward); signal(SIGHUP, forward);
    child_pid = fork();
    if (child_pid < 0) fail("fork");
    if (!child_pid) { argv[0] = image; execv(image, argv); fail("start runtime"); }
    int status;
    while (waitpid(child_pid, &status, 0) < 0) if (errno != EINTR) fail("wait");
    unlink(image); rmdir(temporary);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
