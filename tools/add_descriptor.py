#!/usr/bin/env python3
"""Turn a pasted descriptor dump into a descriptors.h entry.

    add_descriptor.py <name> [file]        # reads stdin if no file
    add_descriptor.py wooting_keyboard dump.txt

Eats whatever the issue tracker hands you: usbhid-dump output, the Windows tool's
"DESCRIPTOR:" blocks, C arrays with 0x and commas, or bare space separated hex.
Non-hex lines are skipped, so you can paste a whole comment and it still works.

Prints the C array and the registry line to add. It deliberately does not edit
descriptors.h for you, because the comment naming the device and issue matters and
only you can write it.
"""
import re
import sys

# a hex byte, optionally 0x prefixed, not part of a longer word
TOKEN = re.compile(r"(?:\b0[xX])?([0-9A-Fa-f]{2})\b")

# usbhid-dump prefixes each block with e.g. "001:004:000:DESCRIPTOR  1719851496.9"
NOISE = re.compile(r"DESCRIPTOR|PATH:|^\s*```|bLength|bDescriptorType|^\s*\(\d+ bytes\)")


def parse_hex(text):
    out = []
    for line in text.splitlines():
        if NOISE.search(line):
            continue

        # a descriptor line is mostly hex; skip prose that happens to contain "ab"
        tokens = TOKEN.findall(line)
        if len(tokens) < 4:
            continue

        stripped = re.sub(r"[\s,]|0[xX]", "", line)
        if not re.fullmatch(r"[0-9A-Fa-f]*", stripped):
            continue

        out.extend(int(t, 16) for t in tokens)
    return out


def sanity(b):
    """Cheap plausibility checks, so a bad paste is caught before it becomes a test."""
    notes = []
    if not b:
        return ["no hex bytes found"]
    if b[0] not in (0x05, 0x06):
        notes.append("does not start with a Usage Page item (05 or 06), check the paste")

    depth, i = 0, 0
    while i < len(b):
        size = b[i] & 0x03
        size = 4 if size == 3 else size
        tag_type = b[i] & 0xFC
        if tag_type == 0xA0:
            depth += 1
        elif tag_type == 0xC0:
            depth -= 1
        i += 1 + size

    if i != len(b):
        notes.append("items do not land exactly on the end (%d vs %d), descriptor may be "
                     "truncated" % (i, len(b)))
    if depth != 0:
        notes.append("collections unbalanced (%+d), descriptor may be incomplete" % depth)
    return notes


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    name = sys.argv[1]
    text = open(sys.argv[2]).read() if len(sys.argv) > 2 else sys.stdin.read()

    b = parse_hex(text)
    notes = sanity(b)

    print("/* TODO: name the device and the issue number */")
    print("static const uint8_t d_%s[] = {" % name)
    for i in range(0, len(b), 16):
        print("    " + " ".join("0x%02X," % x for x in b[i:i + 16]))
    print("};")
    print()
    print("/* add to the descriptors[] table: */")
    print("    D(%s)," % name)
    print()
    print("/* %d bytes */" % len(b), file=sys.stderr)

    for note in notes:
        print("WARNING: %s" % note, file=sys.stderr)
    return 1 if any("no hex" in n for n in notes) else 0


if __name__ == "__main__":
    sys.exit(main())
