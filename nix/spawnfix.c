/* Loaded with ld.so --preload. Telegram re-execs this process's loader
   (/proc/self/exe) as:
       ld-linux -webviewhelper <socket>
   ld.so then tries to load a library named -webviewhelper. Rewrite that
   exec so the real program is ../Telegram, argv0 is webviewhelper (no
   leading dash), and -webviewhelper stays a normal argument. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_helper(char *const argv[]) {
	return argv && argv[0] && argv[1] && !strcmp(argv[1], "-webviewhelper");
}

static char **rewrite(const char *loader, char *const argv[]) {
	char dir[4096];
	snprintf(dir, sizeof dir, "%s", loader);
	char *slash = strrchr(dir, '/');
	if (!slash) {
		return NULL;
	}
	*slash = 0;
	const char *host = NULL;
	if (!access("/usr/lib/libGLX.so.0", R_OK) || !access("/usr/lib/libGL.so.1", R_OK)) {
		host = "/usr/lib";
	} else if (!access("/usr/lib64/libGLX.so.0", R_OK) || !access("/usr/lib64/libGL.so.1", R_OK)) {
		host = "/usr/lib64";
	} else if (!access("/usr/lib/x86_64-linux-gnu/libGLX.so.0", R_OK)
		|| !access("/usr/lib/x86_64-linux-gnu/libGL.so.1", R_OK)) {
		host = "/usr/lib/x86_64-linux-gnu";
	}
	char *libdir = malloc(strlen(dir) + (host ? strlen(host) : 0) + 2);
	char *telegram = malloc(strlen(dir) + 16);
	if (!libdir || !telegram) {
		return NULL;
	}
	if (host) {
		sprintf(libdir, "%s:%s", dir, host);
	} else {
		sprintf(libdir, "%s", dir);
	}
	sprintf(telegram, "%s/../Telegram", dir);

	int extra = 0;
	while (argv[2 + extra]) {
		++extra;
	}
	char **next = calloc((size_t)extra + 8, sizeof(char *));
	if (!next) {
		return NULL;
	}
	int i = 0;
	next[i++] = (char *)loader;
	next[i++] = "--library-path";
	next[i++] = libdir;
	next[i++] = "--argv0";
	next[i++] = "webviewhelper";
	next[i++] = telegram;
	next[i++] = "-webviewhelper";
	for (int k = 0; k < extra; ++k) {
		next[i++] = argv[2 + k];
	}
	next[i] = NULL;
	return next;
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
	static int (*real_execve)(const char *, char *const[], char *const[]) = NULL;
	if (!real_execve) {
		real_execve = dlsym(RTLD_NEXT, "execve");
	}
	if (is_helper(argv)) {
		char **next = rewrite(pathname, argv);
		if (next) {
			return real_execve(pathname, next, envp);
		}
	}
	return real_execve(pathname, argv, envp);
}

int execv(const char *pathname, char *const argv[]) {
	return execve(pathname, argv, environ);
}

int posix_spawn(
		pid_t *pid,
		const char *path,
		const posix_spawn_file_actions_t *file_actions,
		const posix_spawnattr_t *attrp,
		char *const argv[],
		char *const envp[]) {
	static int (*real_spawn)(
		pid_t *,
		const char *,
		const posix_spawn_file_actions_t *,
		const posix_spawnattr_t *,
		char *const[],
		char *const[]) = NULL;
	if (!real_spawn) {
		real_spawn = dlsym(RTLD_NEXT, "posix_spawn");
	}
	if (is_helper(argv)) {
		char **next = rewrite(path, argv);
		if (next) {
			return real_spawn(pid, path, file_actions, attrp, next, envp);
		}
	}
	return real_spawn(pid, path, file_actions, attrp, argv, envp);
}

int posix_spawnp(
		pid_t *pid,
		const char *file,
		const posix_spawn_file_actions_t *file_actions,
		const posix_spawnattr_t *attrp,
		char *const argv[],
		char *const envp[]) {
	static int (*real_spawnp)(
		pid_t *,
		const char *,
		const posix_spawn_file_actions_t *,
		const posix_spawnattr_t *,
		char *const[],
		char *const[]) = NULL;
	if (!real_spawnp) {
		real_spawnp = dlsym(RTLD_NEXT, "posix_spawnp");
	}
	if (is_helper(argv)) {
		char **next = rewrite(file, argv);
		if (next) {
			return real_spawnp(pid, file, file_actions, attrp, next, envp);
		}
	}
	return real_spawnp(pid, file, file_actions, attrp, argv, envp);
}
