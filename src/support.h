/* Helpers the drivers share, so a fix lands once: dup_exact, the exact-size copy that
 * lets ASan's redzone catch a read one byte past the end instead of returning the next
 * case's bytes; run_forked, one case per child so a crash does not hide the remaining
 * thousands, and a failed fork or wait exits rather than scoring the case clean, as a
 * zero status from waitpid(-1) once did; parse_arg, the strtol wrapper (atoi's "abc" is
 * indistinguishable from an explicit 0);
 * parse_iface, the zeroed-interface parse every driver starts from; print_rule, the
 * dashed line under every table header; print_hex, the byte dump. */
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

/* Exact-size copy, len bytes and not one more, so ASan's redzone begins right after the
   last byte; the drivers refuse a case outside its floor and its report[] before this is
   reached (case_len_ok below). An allocation failure is the host's problem, not a
   finding: exit 3, the status cctest and shortreport already used for it. */
static inline uint8_t *dup_exact(const char *prog, const uint8_t *src, int len) {
    uint8_t *copy = malloc((size_t)len);

    if (!copy) {
        fprintf(stderr, "%s: out of memory\n", prog);
        exit(3);
    }
    memcpy(copy, src, (size_t)len);
    return copy;
}

/* Whether a case can be replayed at all: its len is at least the receiver's floor, below
   which the firmware never hands a report to the decoder, and no more than its report[]
   holds. A row outside that is a fault in the table, refused by every driver in the same
   words rather than read past the struct or asserted for an input no device can send. */
static inline bool case_len_ok(int len, int min, size_t cap) {
    return len >= min && (size_t)len <= cap;
}

/* Run fn(arg) in a forked child. Returns 0 if the child exited clean, its exit status if
   not, and 128 + the signal if it was killed. A sanitizer report ends the child with exit
   status 1, not a signal (-fno-sanitize-recover=all, and ASan reports a bad address
   itself), so a finding arrives like any other failure; the signal branch is for abort()
   or a kill from outside. quiet sends the child's output to /dev/null. */
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
   replay tools. Decimal only: base 0 read 010 as 8 and refused 08, and every number these
   tools print or document is decimal. Returns 0 on success. */
static inline int parse_arg(const char *prog, const char *what, const char *s, long lo,
                            long hi, long *out) {
    char *end;

    errno = 0;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "%s: %s=%s is not a decimal number in %ld..%ld\n", prog, what, s, lo,
                hi);
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
