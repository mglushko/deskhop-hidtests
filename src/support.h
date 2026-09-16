/* Helpers the drivers share, so a fix lands once.
 *
 * dup_exact copies a report or descriptor into an allocation of exactly its length,
 * which is what lets ASan's redzone catch a read one byte past the end instead of
 * returning the next case's bytes; six sites used to spell it out, four of them without
 * checking malloc. run_forked runs one case in a child so a crash does not hide the
 * remaining thousands, and a fork or wait that fails exits rather than scoring the case
 * clean, which is what a zero status from waitpid(-1) used to do. parse_arg is fuzz.c's
 * strtol wrapper, here so truncate and shortreport stop using atoi, whose "abc" is
 * indistinguishable from an explicit 0. parse_iface is the zeroed-interface parse every
 * driver starts from, print_rule the dashed line under every table header, and print_hex
 * the byte dump four of them had hand-rolled with their own padding constants.
 */
#pragma once

#include "main.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exact-size copy: len bytes and not one more, so ASan's redzone begins right after the
   last byte of the report. That holds at len 0 too, where malloc(0) under ASan is a
   region no read may touch; every caller has bounded len below already. An allocation
   failure is the host's problem, not a finding: exit 3, the status cctest and
   shortreport already used for it. */
static inline uint8_t *dup_exact(const char *prog, const uint8_t *src, int len) {
    uint8_t *copy = malloc((size_t)len);

    if (!copy) {
        fprintf(stderr, "%s: out of memory\n", prog);
        exit(3);
    }
    memcpy(copy, src, (size_t)len);
    return copy;
}

/* Run fn(arg) in a forked child. Returns 0 if the child exited clean, its exit status if
   not, and 128 + the signal if it was killed. A sanitizer report is the first kind, not
   the second: under the options the Makefile sets, ASan and UBSan end the child with a
   plain status of 1, so a finding arrives like any other non-zero exit and the signal
   branch is for a real crash. quiet sends the child's output to /dev/null. */
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
   replay tools. `base` is strtol's: 10 for the case indices, lengths and entry numbers
   the replay tools print in decimal, where base 0 would read 010 as 8 and refuse 08;
   0 for fuzz, so a seed can still be given in hex. Returns 0 on success. */
static inline int parse_arg(const char *prog, const char *what, const char *s, int base,
                            long lo, long hi, long *out) {
    char *end;

    errno = 0;
    long v = strtol(s, &end, base);

    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "%s: %s=%s is not a number in %ld..%ld\n", prog, what, s, lo, hi);
        return 1;
    }

    *out = v;
    return 0;
}

/* The parse every driver starts from: a zeroed interface at the given protocol, fed the
   descriptor. iface is the caller's, usually static, because hid_interface_t is large. */
static inline void parse_iface(hid_interface_t *iface, const uint8_t *desc, int len,
                               uint8_t protocol) {
    memset(iface, 0, sizeof(*iface));
    iface->protocol = protocol;
    parse_report_descriptor(iface, desc, len);
}

/* A table rule: two spaces of indent, then n dashes. */
static inline void print_rule(int n) {
    printf("  ");
    for (int i = 0; i < n; i++)
        printf("-");
    printf("\n");
}

/* len bytes as "XX " each, padded with spaces to pad_to columns when that is wider. */
static inline void print_hex(const uint8_t *b, int len, int pad_to) {
    int printed = 0;

    for (int i = 0; i < len; i++)
        printed += printf("%02X ", b[i]);
    for (; printed < pad_to; printed++)
        printf(" ");
}
