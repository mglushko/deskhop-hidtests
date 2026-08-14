#!/usr/bin/env python3
"""Check include/harness.h's constants against the vendored TinyUSB hid.h.

    check_constants.py <harness.h> <tinyusb/class/hid/hid.h> [parser.c ...]

harness.h hand-copies the item tags, usage pages and usages that hid_parser.c and
hid_report.c reach for, because pulling in the real TinyUSB would drag the whole
Pico SDK onto the host. That copy is the harness's one unchecked assumption, and
the failure mode is nasty: a wrong value does not break the build or crash
anything, it shifts an offset or misclassifies a usage and every target reports a
plausible wrong answer at once.

A constant the parser uses but harness.h omits is already a compile error, so this
only has to catch disagreements in value, plus names that have drifted out of
TinyUSB entirely.

Any .c files given after the two headers are scanned to work out which constants
the parser actually depends on, so a mismatch in a load-bearing one can be called
out as such rather than buried among the ones that are only there for completeness.
"""
import re
import sys

PREFIXES = ("RI_", "HID_USAGE_", "HID_PROTOCOL_", "HID_ITF_PROTOCOL_")

COMMENTS = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
ENUM_BODY = re.compile(r"\benum\b[^{]*\{(.*?)\}", re.S)
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+(\(?\s*(?:0[xX][0-9A-Fa-f]+|\d+)\s*\)?)[ \t]*$", re.M)


def interesting(name):
    return name.startswith(PREFIXES)


def parse(text):
    """Name -> int for every enumerator and simple #define we care about.

    Enumerators without an explicit initialiser take the previous value plus one,
    exactly as C does. Resolving those matters: TinyUSB writes several of these
    lists with only the first entry numbered, and treating a bare name as absent
    would report a mismatch that is not there.
    """
    text = COMMENTS.sub(" ", text)
    out = {}

    for body in ENUM_BODY.findall(text):
        nxt = 0
        # split on top-level commas; no nested braces appear in these enums, but
        # parentheses can wrap a value
        for item in re.split(r",(?![^()]*\))", body):
            item = item.strip()
            if not item:
                continue
            if "=" in item:
                name, _, val = item.partition("=")
                name, val = name.strip(), val.strip()
                try:
                    value = int(val.strip("()").strip(), 0)
                except ValueError:
                    # a computed initialiser; skip it and stop trusting the
                    # running counter for the rest of this enum
                    nxt = None
                    continue
            else:
                name, value = item, nxt
                if value is None:
                    continue
            if not re.fullmatch(r"[A-Za-z_]\w*", name):
                nxt = None
                continue
            out.setdefault(name, value)
            nxt = value + 1

    for name, val in DEFINE.findall(text):
        try:
            out.setdefault(name, int(val.strip("()").strip(), 0))
        except ValueError:
            pass

    return {k: v for k, v in out.items() if interesting(k)}


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)

    harness_path, tusb_path, sources = sys.argv[1], sys.argv[2], sys.argv[3:]

    harness = parse(open(harness_path).read())
    tusb = parse(open(tusb_path).read())

    used = set()
    for path in sources:
        try:
            used |= set(re.findall(r"\b(?:%s)\w*" % "|".join(PREFIXES), open(path).read()))
        except OSError:
            pass

    mismatched, absent = [], []
    for name in sorted(harness):
        if name not in tusb:
            absent.append(name)
        elif harness[name] != tusb[name]:
            mismatched.append((name, harness[name], tusb[name]))

    checked = len(harness) - len(absent)
    print("  harness.h defines %d constants, %d checked against %s"
          % (len(harness), checked, tusb_path))
    if used:
        print("  %d of them are referenced by the parser sources" % len(set(harness) & used))

    if mismatched:
        print("\n  VALUE MISMATCH:")
        for name, h, t in mismatched:
            flag = "  <- used by the parser" if name in used else ""
            print("    %-34s harness 0x%02X, tinyusb 0x%02X%s" % (name, h, t, flag))

    if absent:
        print("\n  defined by harness.h but not found in tinyusb hid.h:")
        for name in absent:
            flag = "  <- used by the parser" if name in used else ""
            print("    %s%s" % (name, flag))
        print("    (renamed or removed upstream, or defined in another header)")

    if mismatched:
        print("\n  RESULT: harness.h disagrees with TinyUSB, every target is suspect")
        return 1
    if absent:
        print("\n  RESULT: values all agree, but see the names above")
        return 0

    print("\n  RESULT: all %d agree with TinyUSB" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
