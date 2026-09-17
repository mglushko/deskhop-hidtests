/* What a case table's gates kept out of this build, printed last in a run. The tables
 * drop a device behind #ifdef HARNESS_* when the target lacks the bound it needs, so one
 * known overread does not take the whole run down; each such gate adds its device to a
 * list here in its closed form. A probe that stops matching the target (a parameter
 * renamed upstream, say) then shows as a line, not as a quietly smaller denominator. */
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
