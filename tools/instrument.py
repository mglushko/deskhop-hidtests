#!/usr/bin/env python3
"""Instrument a copy of hid_parser.c so every access into parser_state.usages[]
reports its absolute index to dbg_touch() before happening, then gets clamped so
the harness survives and can keep fuzzing instead of dying on the first overflow.

    instrument.py <hid_parser.c> <out.c>

Works on both the pre-fix and post-fix shapes of the file. Sites that only exist
in one shape are optional; at least one must match or it exits non-zero, which
catches the case where upstream restructured the parser and this tool has gone
stale.
"""
import os
import sys

HELPER = '''
extern void dbg_touch(parser_state_t *parser, long abs_idx);

/* Report where this access lands relative to the start of usages[], then clamp
   into range. Clamping is what lets one process fuzz thousands of descriptors
   that would otherwise corrupt the parser's own state on the first one. */
static inline uint16_t *dbg_slot(parser_state_t *parser, uint16_t *p, long i) {
    long abs = (long)(p - parser->usages) + i;

    dbg_touch(parser, abs);

    if (abs < 0)
        abs = 0;
    if (abs >= HID_MAX_USAGES)
        abs = HID_MAX_USAGES - 1;

    return parser->usages + abs;
}
'''

SITES = [
    # (tag, old, new, required)
    ("update_usage",
     "*(parser->p_usage + i) = *(parser->p_usage + i - 1);",
     "*dbg_slot(parser, parser->p_usage, i) = *dbg_slot(parser, parser->p_usage, i - 1);",
     True),

    # pre-fix only: store_element reads the usage itself
    ("store_element_read",
     ".usage        = *(parser->p_usage + i),",
     ".usage        = *dbg_slot(parser, parser->p_usage, i),",
     False),

    # post-fix only: get_usage() returns it
    ("get_usage_read",
     "return *(parser->p_usage + i);",
     "return *dbg_slot(parser, parser->p_usage, i);",
     False),

    ("local_push",
     "*(parser->p_usage + parser->usage_count++) = item->val;",
     "{ *dbg_slot(parser, parser->p_usage, (long)parser->usage_count) = item->val;"
     " parser->usage_count++; }",
     True),

    ("carry",
     "*parser->p_usage = *(parser->p_usage - parser->usage_count);",
     "*dbg_slot(parser, parser->p_usage, 0) ="
     " *dbg_slot(parser, parser->p_usage, -(long)parser->usage_count);",
     True),

    # the cursor advance itself can walk out of the array
    ("advance",
     "parser->p_usage += parser->usage_count;",
     "parser->p_usage += parser->usage_count;"
     " dbg_touch(parser, (long)(parser->p_usage - parser->usages));",
     True),
]


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)

    src = open(sys.argv[1]).read()

    # dbg_slot needs parser_state_t, so it goes after the include, not at the top
    src = src.replace('#include "main.h"', '#include "main.h"\n' + HELPER, 1)

    applied, missing = [], []
    for tag, old, new, required in SITES:
        if old in src:
            src = src.replace(old, new, 1)
            applied.append(tag)
        elif required:
            missing.append(tag)

    if missing:
        # Bail before writing anything. Writing first and returning non-zero after
        # leaves a file newer than its prerequisites, so the next make skips this
        # rule and links a half-instrumented parser - and fuzz then under-reports
        # out-of-bounds accesses, which is the exact false negative it exists to
        # prevent. The Makefile also sets .DELETE_ON_ERROR:; this is the other half.
        print("instrumenting %s FAILED" % sys.argv[1])
        print("  MISSING REQUIRED SITES: %s" % ", ".join(missing))
        print("  found: %s" % (", ".join(applied) or "none"))
        print("  the parser was restructured upstream; update tools/instrument.py")
        return 1

    # write via a temp path so an interrupted write cannot leave a usable-looking file
    tmp = sys.argv[2] + ".tmp"
    with open(tmp, "w") as f:
        f.write(src)
    os.replace(tmp, sys.argv[2])

    print("instrumented %s -> %s" % (sys.argv[1], sys.argv[2]))
    print("  sites: %s" % ", ".join(applied))
    return 0


if __name__ == "__main__":
    sys.exit(main())
