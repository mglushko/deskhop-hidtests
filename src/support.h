/* Helpers the drivers share, so a fix lands once: dup_exact, the exact-size copy that
 * lets ASan's redzone catch a read one byte past the end instead of returning the next
 * case's bytes, zero bytes included, with free_exact to release it; run_forked, one case
 * per child so a crash does not hide the remaining thousands, and a failed fork or wait
 * exits rather than scoring the case clean, as a zero status from waitpid(-1) once did,
 * a quiet child's sanitizer report is read back and named, and a child that hangs is
 * stopped and named as hung; sweep_unsymbolized and sweep_repro, what keeps a sweep of
 * thousands fast and its one replayed failure readable; tally_add and print_tally, the
 * count per kind of failure; parse_arg, the strtol wrapper (atoi's "abc" is
 * indistinguishable from an explicit 0); parse_iface, the zeroed-interface parse every
 * driver starts from; print_rule, the dashed line under every table header; print_hex,
 * the byte dump. */
#pragma once

#include "main.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* How long one forked case may run. A case takes milliseconds, a few hundred under ASan on
   a slow runner, so this only ever fires on a hang: a parse loop that stops advancing, say.
   Without it one such case stalls the sweep for good, and a CI run spends its whole time
   limit printing nothing about where. alarm() survives exec, so sweep_repro's replay is
   bounded by it too. */
#ifndef SWEEP_CHILD_TIMEOUT_S
#define SWEEP_CHILD_TIMEOUT_S 10
#endif

/* Exact-size copy, len bytes and not one more, so ASan's redzone begins right after the
   last byte; the drivers refuse a case outside its floor and its report[] before this is
   reached (case_len_ok below). An allocation failure is the host's problem, not a
   finding: exit 3, the status cctest and shortreport already used for it.

   Zero bytes is a real length: TinyUSB hands the report callback a zero-length transfer
   after a STALL, a zero-length packet or three failed transactions. malloc(0) will not do
   for it, since ASan lets a read of malloc(0)[0] through; the copy is instead the address
   one past a 1-byte block, where any read lands in the redzone. Free it with free_exact.
   Not inlined: inlined, GCC follows that pointer into callers that never pass 0 and warns
   the memory behind it may be uninitialized, which for a zero-length report is the point.
   unused, since plain static would warn in each driver that never calls it. */
__attribute__((noinline, unused))
static uint8_t *dup_exact(const char *prog, const uint8_t *src, int len) {
    uint8_t *copy = malloc(len ? (size_t)len : 1);

    if (!copy) {
        fprintf(stderr, "%s: out of memory\n", prog);
        exit(3);
    }
    if (len) {
        memcpy(copy, src, (size_t)len);
        return copy;
    }
    return copy + 1;
}

/* Release what dup_exact returned for the same len. */
static inline void free_exact(uint8_t *copy, int len) {
    free(len ? copy : copy - 1);
}

/* Whether a case can be replayed at all: its len is at least the receiver's floor, below
   which the firmware never hands a report to the decoder, and no more than its report[]
   holds. A row outside that is a fault in the table, refused by every driver in the same
   words rather than read past the struct or asserted for an input no device can send. */
static inline bool case_len_ok(int len, int min, size_t cap) {
    return len >= min && (size_t)len <= cap;
}

/* Copy s into out with every run of digits, hex included, written as N, so "shift
   exponent 40" and "shift exponent 36" count as one kind and an address never splits one. */
static inline void squash_numbers(const char *s, size_t n, char *out, size_t len) {
    size_t o = 0;

    for (size_t i = 0; i < n && o + 1 < len; i++) {
        if (isdigit((unsigned char)s[i])) {
            out[o++] = 'N';
            while (i + 1 < n && (isxdigit((unsigned char)s[i + 1]) || s[i + 1] == 'x'))
                i++;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
}

/* Name a failure from what the child printed, so a sweep's summary says what it measured
   rather than calling every non-zero status an overread: ASan's error kind and whether
   the access read or wrote, UBSan's message with its numbers squashed, or the bare status
   when neither sanitizer spoke. */
static inline void classify_failure(const char *text, int status, char *why, size_t len) {
    static const char asan[] = "ERROR: AddressSanitizer: ", ubsan[] = "runtime error: ";
    const char *p;

    if ((p = strstr(text, asan))) {
        p += sizeof(asan) - 1;
        const char *rw = strstr(p, "READ of size")    ? "read"
                         : strstr(p, "WRITE of size") ? "write"
                                                      : "access";
        snprintf(why, len, "ASan %.*s, %s", (int)strcspn(p, " \n"), p, rw);
    } else if ((p = strstr(text, ubsan))) {
        char msg[112];

        p += sizeof(ubsan) - 1;
        squash_numbers(p, strcspn(p, "\n"), msg, sizeof(msg));
        snprintf(why, len, "UBSan %s", msg);
    } else if (status == 128 + SIGALRM) {
        snprintf(why, len, "hung: no result within %d s", SWEEP_CHILD_TIMEOUT_S);
    } else if (status >= 128) {
        snprintf(why, len, "killed by signal %d", status - 128);
    } else {
        snprintf(why, len, "exit status %d, no sanitizer report", status);
    }
}

/* Run fn(arg) in a forked child. Returns 0 if the child exited clean, its exit status if
   not, and 128 + the signal if it was killed. A sanitizer report ends the child with a
   non-zero exit status, not a signal (-fno-sanitize-recover=all, and ASan reports a bad
   address itself), so a finding arrives like any other failure; the signal branch is for
   abort(), a kill from outside, or the SWEEP_CHILD_TIMEOUT_S alarm that stops a child
   that hangs. quiet sends the child's stdout to /dev/null and its stderr down a pipe,
   read back here, so a failure can be named in why (see classify_failure); why may be
   NULL, and is left alone when the child came back clean.
   The pipe is drained before waitpid, or a report longer than the pipe holds would block
   the child and the wait both. */
static inline int run_forked(const char *prog, void (*fn)(const void *), const void *arg,
                             int quiet, char *why, size_t why_len) {
    int fds[2] = {-1, -1};

    if (quiet && pipe(fds) < 0) {
        fprintf(stderr, "%s: pipe: %s\n", prog, strerror(errno));
        exit(3);
    }

    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (pid == 0) {
        if (quiet) {
            int null = open("/dev/null", O_WRONLY);
            if (null >= 0)
                dup2(null, 1);
            dup2(fds[1], 2);
            close(fds[0]);
            close(fds[1]);
        }
        alarm(SWEEP_CHILD_TIMEOUT_S);
        fn(arg);
        _exit(0);
    }

    /* The first few KiB carry the error line and the access; the rest is stack. */
    char   text[4096];
    size_t got = 0;

    if (quiet) {
        close(fds[1]);
        for (;;) {
            char    sink[512];
            size_t  room = sizeof(text) - 1 - got;
            ssize_t r    = read(fds[0], room ? text + got : sink, room ? room : sizeof(sink));

            if (r > 0) {
                if (room)
                    got += (size_t)r;
            } else if (r == 0 || errno != EINTR) {
                break;
            }
        }
        close(fds[0]);
    }
    text[got] = '\0';

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "%s: waitpid: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;

    int rc = WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
    if (why && why_len)
        classify_failure(text, rc, why, why_len);
    return rc;
}

/* Append opt to the sanitizer options in env var name. A later option overrides an
   earlier one of the same name, so appending is how a value is forced. */
static inline void add_sanitizer_option(const char *name, const char *opt) {
    const char *old = getenv(name);
    char        buf[1024];

    snprintf(buf, sizeof(buf), "%s%s%s", old ? old : "", old && *old ? ":" : "", opt);
    setenv(name, buf, 1);
}

/* A sweep forks thousands of children, and on a tree with the bug it looks for, thousands
   of them fail. Symbolizing every failure's stack, only for the report to be thrown away,
   was nearly all of the run: truncate took about ten minutes with it and 23 seconds
   without, finding the same 5069. Sanitizer options are read once, at startup, so the
   sweep executes itself again with symbolize=0 before it forks anything; HARNESS_SWEEP
   marks the second start. Options that already name symbolize are the caller's choice
   and are left alone. If the exec fails the sweep runs as it is, only slower. */
static inline void sweep_unsymbolized(char **argv) {
    const char *a = getenv("ASAN_OPTIONS"), *u = getenv("UBSAN_OPTIONS");

    if (getenv("HARNESS_SWEEP") || (a && strstr(a, "symbolize=")) ||
        (u && strstr(u, "symbolize=")))
        return;

    setenv("HARNESS_SWEEP", "1", 1);
    add_sanitizer_option("ASAN_OPTIONS", "symbolize=0");
    add_sanitizer_option("UBSAN_OPTIONS", "symbolize=0");
    fflush(stdout);
    execv("/proc/self/exe", argv);
    execv(argv[0], argv);
    unsetenv("HARNESS_SWEEP");
}

/* Replay one failure through the tool's own single-case command line, args, in a fresh
   process with symbolization back on, so the stack printed is readable and the command
   the summary then suggests is the one that just ran. Returns its status as run_forked
   does. */
static inline int sweep_repro(const char *prog, char **args) {
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (pid == 0) {
        add_sanitizer_option("ASAN_OPTIONS", "symbolize=1");
        add_sanitizer_option("UBSAN_OPTIONS", "symbolize=1");
        alarm(SWEEP_CHILD_TIMEOUT_S);
        execv("/proc/self/exe", args);
        execv(args[0], args);
        fprintf(stderr, "%s: cannot re-run itself to replay the failure: %s\n", prog,
                strerror(errno));
        _exit(3);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "%s: waitpid: %s\n", prog, strerror(errno));
        exit(3);
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 0;
}

/* Failures counted by kind, in the order first seen. Kinds past the table's size are
   counted together, and said to be. */
#define TALLY_KINDS 16

typedef struct {
    char why[128];
    long n;
} tally_t;

static inline void tally_add(tally_t *t, const char *why) {
    int i;

    for (i = 0; i < TALLY_KINDS - 1 && t[i].n; i++)
        if (strcmp(t[i].why, why) == 0)
            break;
    if (i == TALLY_KINDS - 1 && t[i].n == 0)
        snprintf(t[i].why, sizeof(t[i].why), "%s", "other kinds, past the table's size");
    else if (!t[i].n)
        snprintf(t[i].why, sizeof(t[i].why), "%s", why);
    t[i].n++;
}

/* One line per kind, under the total they add up to. tools/ratchet.py reads these back,
   so the shape is load bearing: four spaces, the count, two spaces, the kind. */
static inline void print_tally(const tally_t *t) {
    for (int i = 0; i < TALLY_KINDS && t[i].n; i++)
        printf("    %6ld  %s\n", t[i].n, t[i].why);
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
