#!/usr/bin/env python3
"""Instrument a copy of hid_parser.c so every access into parser_state.usages[]
reports its absolute index to dbg_touch() before happening, then gets clamped so
the harness survives and can keep fuzzing instead of dying on the first overflow.

    instrument.py <hid_parser.c> <out.c>

Works on the three shapes the file has had: before the usage-array bound, with PR
#361's usages_left(), and with upstream 1e31d10's rewrite of that bound. A site that
exists in only some shapes is optional where its shape is absent; every access that
is there must be matched, or it exits non-zero, which catches the case where
upstream restructured the parser again and this tool has gone stale.
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

# (tag, old, new, group)
#
# group is what the site belongs to, and every group must end up with at least one
# match or the run fails. A site in a group of its own is simply required.
#
# The read of the usage is spelled differently in each shape of the parser, so those
# sites share a group: any one of them counts, none being present does not.
# Marking them individually optional - which is what this used to do - meant a
# restructure upstream could leave the read uninstrumented while every required
# site still matched, and fuzz would then under-report out-of-bounds accesses
# without a word. That is the exact false negative this tool exists to prevent.
SITES = [
    # pre-fix and #361: update_usage() writes the previous element's usage forward
    ("update_usage",
     "*(parser->p_usage + i) = *(parser->p_usage + i - 1);",
     "*dbg_slot(parser, parser->p_usage, i) = *dbg_slot(parser, parser->p_usage, i - 1);",
     "update_usage"),

    # pre-fix only: store_element reads the usage itself
    ("store_element_read",
     ".usage        = *(parser->p_usage + i),",
     ".usage        = *dbg_slot(parser, parser->p_usage, i),",
     "usage_read"),

    # #361: get_usage() returns it
    ("get_usage_read",
     "return *(parser->p_usage + i);",
     "return *dbg_slot(parser, parser->p_usage, i);",
     "usage_read"),

    # 1e31d10: get_usage() takes the slot's address, clamps it to the array, then reads
    ("get_usage_slot",
     "uint16_t *slot = parser->p_usage + idx;",
     "uint16_t *slot = dbg_slot(parser, parser->p_usage, idx);",
     "usage_read"),

    ("local_push",
     "*(parser->p_usage + parser->usage_count++) = item->val;",
     "{ *dbg_slot(parser, parser->p_usage, (long)parser->usage_count) = item->val;"
     " parser->usage_count++; }",
     "local_push"),

    # pre-fix and #361: the carry copies the first usage of the finished block
    ("carry_first",
     "*parser->p_usage = *(parser->p_usage - parser->usage_count);",
     "*dbg_slot(parser, parser->p_usage, 0) ="
     " *dbg_slot(parser, parser->p_usage, -(long)parser->usage_count);",
     "carry"),

    # 1e31d10: it copies the last one, reading the slot behind the cursor
    ("carry_last",
     "*parser->p_usage = *(parser->p_usage - 1);",
     "*dbg_slot(parser, parser->p_usage, 0) = *dbg_slot(parser, parser->p_usage, -1);",
     "carry"),

    # the cursor advance itself can walk out of the array
    ("advance",
     "parser->p_usage += parser->usage_count;",
     "parser->p_usage += parser->usage_count;"
     " dbg_touch(parser, (long)(parser->p_usage - parser->usages));",
     "advance"),

    # 1e31d10 only: a full array pins the cursor on the last slot instead of advancing
    ("pin",
     "parser->p_usage = parser->usages + HID_MAX_USAGES - 1;",
     "parser->p_usage = parser->usages + HID_MAX_USAGES - 1;"
     " dbg_touch(parser, HID_MAX_USAGES - 1);",
     "advance"),
]

# A group whose sites exist only in some shapes of the file is required only while the
# shape is: the marker is text that proves the shape is present. update_usage() was
# removed by 1e31d10, which reads the last declared usage instead of writing it forward,
# so on that shape there is nothing to instrument and nothing missing. Without this the
# tool refused the current upstream parser outright; with the group merely optional it
# would stay silent if a #361-shaped tree ever lost the site to reformatting.
GROUP_MARKER = {
    "update_usage": "void update_usage(",
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)

    src = open(sys.argv[1]).read()
    orig = src

    # This is a modified copy of a GPLv3 source file, so it is a derivative work
    # and carries the same terms. Saying so at the top keeps that visible to
    # anyone reading it out of the build directory without the repo around it.
    src = ("/* GENERATED by tools/instrument.py from %s - do not edit.\n\n"
           "   A modified copy of a DeskHop source file\n"
           "   (https://github.com/hrvach/deskhop), Copyright (c) 2025 Hrvoje Cavrak,\n"
           "   licensed under the GNU General Public License version 3. This generated\n"
           "   file is a derivative work and is covered by the same licence; see LICENSE.\n"
           " */\n" % sys.argv[1]) + src

    # dbg_slot needs parser_state_t, so it goes after the include, not at the top
    src = src.replace('#include "main.h"', '#include "main.h"\n' + HELPER, 1)

    applied, seen_groups, all_groups = [], set(), []
    for tag, old, new, group in SITES:
        if group not in all_groups:
            all_groups.append(group)

        # replace every occurrence, not just the first: a second copy of the same
        # expression would otherwise stay uninstrumented and silently drop its
        # accesses from the count
        hits = src.count(old)
        if hits:
            src = src.replace(old, new)
            applied.append("%s x%d" % (tag, hits) if hits > 1 else tag)
            seen_groups.add(group)

    # required unless the group's marker says this shape never had the site
    def required(group):
        marker = GROUP_MARKER.get(group)
        return marker is None or marker in orig

    missing = [g for g in all_groups if g not in seen_groups and required(g)]
    absent  = [g for g in all_groups if g not in seen_groups and not required(g)]

    if missing:
        # Bail before writing anything. Writing first and returning non-zero after
        # leaves a file newer than its prerequisites, so the next make skips this
        # rule and links a half-instrumented parser - and fuzz then under-reports
        # out-of-bounds accesses, which is the exact false negative it exists to
        # prevent. The Makefile also sets .DELETE_ON_ERROR:; this is the other half.
        print("instrumenting %s FAILED" % sys.argv[1])
        print("  NO SITE MATCHED IN %d GROUP(S):" % len(missing))
        for group in missing:
            alts = [tag for tag, _, _, g in SITES if g == group]
            print("    %-16s tried: %s" % (group, ", ".join(alts)))
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
    if absent:
        print("  absent in this shape: %s" % ", ".join(absent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
