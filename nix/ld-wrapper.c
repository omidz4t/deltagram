/* Stand in for the bundled ld-linux. Telegram re-execs /proc/self/exe,
   which is this loader, with "-webviewhelper" as argv[1]. A real ld-linux
   treats that as a library name. Rewrite that spawn, and otherwise pass
   the original arguments through to the real loader. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *what) {
	perror(what);
	_exit(127);
}

int main(int argc, char **argv) {
	char self[PATH_MAX];
	const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n < 0) {
		fail("readlink");
	}
	self[n] = 0;

	char dir[PATH_MAX];
	memcpy(dir, self, (size_t)n + 1);
	char *slash = strrchr(dir, '/');
	if (!slash) {
		fail("dirname");
	}
	*slash = 0;

	char real_ld[PATH_MAX];
	char telegram[PATH_MAX];
	if (snprintf(real_ld, sizeof real_ld, "%s/ld-linux-x86-64.so.2.real", dir)
			>= (int)sizeof real_ld
		|| snprintf(telegram, sizeof telegram, "%s/../Telegram", dir)
			>= (int)sizeof telegram) {
		fail("path");
	}

	if (argc >= 2 && strcmp(argv[1], "-webviewhelper") == 0) {
		/* real-ld --library-path dir --argv0 webviewhelper telegram -webviewhelper ... */
		const int extra = argc - 2;
		char **next = calloc((size_t)extra + 8, sizeof(char *));
		if (!next) {
			fail("calloc");
		}
		int i = 0;
		next[i++] = real_ld;
		next[i++] = "--library-path";
		next[i++] = dir;
		next[i++] = "--argv0";
		next[i++] = "webviewhelper";
		next[i++] = telegram;
		next[i++] = "-webviewhelper";
		for (int k = 2; k < argc; ++k) {
			next[i++] = argv[k];
		}
		next[i] = NULL;
		execv(real_ld, next);
		fail("exec helper");
	}

	char **next = calloc((size_t)argc + 1, sizeof(char *));
	if (!next) {
		fail("calloc");
	}
	next[0] = real_ld;
	for (int k = 1; k < argc; ++k) {
		next[k] = argv[k];
	}
	next[argc] = NULL;
	execv(real_ld, next);
	fail("exec");
}
