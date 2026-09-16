/* What a case table's gates kept out of this build, for the last line of a run.
 *
 * The case tables drop a device behind #ifdef HARNESS_* when the target lacks the bound
 * that device needs, so that one known overread does not take the whole run down. Each
 * such gate adds its device to a list here in its closed form, and the binaries print the
 * list last. A probe that stops matching the target - a parameter renamed upstream, say -
 * then shows up as a line in the output rather than as a quietly smaller denominator.
 */
#pragma once

#include <stdio.h>

typedef struct {
    const char *device;
    const char *reason;
} kept_out_t;

/* Lists are NULL-terminated so an open gate leaves a valid, empty list. */
static inline void print_kept_out(const kept_out_t *list, const char *indent) {
    for (; list->device; list++)
        printf("%s%s, %s kept out\n", indent, list->reason, list->device);
}
