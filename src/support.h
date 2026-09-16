/* Helpers the drivers share, so a fix lands once.
 *
 * dup_exact copies a report or descriptor into an allocation of exactly its length,
 * which is what lets ASan's redzone catch a read one byte past the end instead of
 * returning the next case's bytes; six sites used to spell it out, four of them without
 * checking malloc. run_forked runs one case in a child so a crash does not hide the
 * remaining thousands, and a fork or wait that fails exits rather than scoring the case
 * clean, which is what a zero status from waitpid(-1) used to do. parse_arg is fuzz.c's
 * strtol wrapper, here so truncate and shortreport stop using atoi, whose "abc" is
 * indistinguishable from an explicit 0.
 */
#pragma once

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exact-size copy. An allocation failure is the host's problem, not a finding: exit 3,
   the status cctest and shortreport already used for it. */
static inline uint8_t *dup_exact(const char *prog, const uint8_t *src, int len) {
    uint8_t *copy = malloc(len > 0 ? (size_t)len : 1);

    if (!copy) {
        fprintf(stderr, "%s: out of memory\n", prog);
        exit(3);
    }
    if (len > 0)
        memcpy(copy, src, (size_t)len);
    return copy;
}

/* Run fn(arg) in a forked child. Returns 0 if the child exited clean, its exit status if
   not, and 128 + the signal if it was killed, which is what an ASan abort looks like from
   outside. quiet sends the child's output to /dev/null. */
static inline int run_forked(const char *prog, void (*fn)(const void *), const void *arg,
                             int quiet) {
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (pid == 0) {
        if (quiet) {
            int null = open("/dev/null", O_WRONLY);
            if (null >= 0) {
                dup2(null, 2);
                dup2(null, 1);
            }
        }
        fn(arg);
        _exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "%s: waitpid: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

/* strtol, not atol: atol("abc") is 0 and indistinguishable from an explicit 0, and a fuzz
   run given that once ran zero descriptors and reported every access in bounds. `what` is
   the argument's name as the user knows it: N or SEED for fuzz, case or length for the
   replay tools. Returns 0 on success. */
static inline int parse_arg(const char *prog, const char *what, const char *s, long lo,
                            long hi, long *out) {
    char *end;

    errno = 0;
    long v = strtol(s, &end, 0);

    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "%s: %s=%s is not a number in %ld..%ld\n", prog, what, s, lo, hi);
        return 1;
    }

    *out = v;
    return 0;
}
