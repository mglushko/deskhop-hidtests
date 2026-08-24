#!/usr/bin/env python3
"""Turn a pasted descriptor dump into a descriptors.h entry.

    add_descriptor.py <name> [file]        # reads stdin if no file
    add_descriptor.py wooting_keyboard dump.txt
    add_descriptor.py --selftest           # check the reader against known dump shapes

Eats whatever the issue tracker hands you: usbhid-dump output, the Windows tool's
"DESCRIPTOR:" blocks, C arrays with 0x and commas, decoded arrays with the item
spelled out in a trailing comment, or bare space separated hex. Non-hex lines are
skipped, so you can paste a whole comment and it still works.

Prints the C array and the registry line to add. It deliberately does not edit
descriptors.h for you, because the comment naming the device and issue matters and
only you can write it.

Anything it drops, it says so on stderr. It used to drop two common shapes in
silence - a decoded array, and a dump whose last line is a lone C0 - and both came
back as a short descriptor that passed every check in sanity(), because losing a
Collection and its End Collection together leaves the rest still balanced.
"""
import os
import re
import sys

# a hex byte, optionally 0x prefixed, not part of a longer word
TOKEN = re.compile(r"(?:\b0[xX])?([0-9A-Fa-f]{2})\b")

# usbhid-dump prefixes each block with e.g. "001:004:000:DESCRIPTOR  1719851496.9"
NOISE = re.compile(r"DESCRIPTOR|PATH:|^\s*```|bLength|bDescriptorType|^\s*\(\d+ bytes\)")

# C comments in a pasted array: "0x05, 0x01,  /* Usage Page (Generic Desktop) */".
# A published dump often decodes every item this way, and before these were stripped
# the whole line failed the "is this line nothing but hex" test below and was dropped
# as prose - silently, and in a way sanity() could not see. See parse_hex.
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")

# what separates bytes in every format worth reading: spaces, commas, 0x prefixes
SEPARATORS = re.compile(r"[\s,]|0[xX]")

# how many bytes a run of hex-only lines needs before it is taken as descriptor data
MIN_BLOCK = 4


def parse_hex(text, notes=None):
    """Every descriptor byte in a pasted dump, in order.

    Lines that are not descriptor data are skipped, so a whole issue comment can be
    piped in. Judging each line on its own was the problem: it cannot tell a dump whose
    final line is a lone C0 from the "de ad" inside a sentence, and it threw away both.
    So lines are classified first and decided in runs - a contiguous block of hex-only
    lines is descriptor data if the block as a whole carries at least MIN_BLOCK bytes,
    however few any one line holds.

    That covers the two shapes that used to be dropped silently, each of which produced
    a short descriptor that still looked plausible:

      - a decoded array, published one item per line with a trailing C comment. The
        comment is stripped, and two bytes on a line no longer disqualifies it.
      - a dump whose last line is a lone C0, which now belongs to the block above it.

    Anything appended to notes is a run that looked like data and was dropped anyway.
    """
    # Whole text first: a block comment can span lines. Replaced with a space rather
    # than deleted so bytes on either side of it cannot fuse into one token.
    text = BLOCK_COMMENT.sub(" ", text)

    # tokens for a data line, None for anything that breaks a run
    classified = []
    for raw in text.splitlines():
        if NOISE.search(raw):
            classified.append(None)
            continue

        line = LINE_COMMENT.sub(" ", raw)
        tokens = TOKEN.findall(line)
        if not tokens:
            classified.append(None)
            continue

        # a descriptor line is nothing but hex; skip prose that happens to contain "ab"
        if not re.fullmatch(r"[0-9A-Fa-f]*", SEPARATORS.sub("", line)):
            classified.append(None)
            if notes is not None and len(tokens) >= MIN_BLOCK:
                notes.append("skipped a line holding %d bytes and other text: %s"
                             % (len(tokens), raw.strip()))
            continue

        classified.append(tokens)

    out, i = [], 0
    while i < len(classified):
        if classified[i] is None:
            i += 1
            continue

        run = []
        while i < len(classified) and classified[i] is not None:
            run.extend(classified[i])
            i += 1

        if len(run) >= MIN_BLOCK:
            out.extend(int(t, 16) for t in run)
        elif notes is not None:
            notes.append("skipped %d stray hex byte%s (%s), too few together to be "
                         "descriptor data" % (len(run), "" if len(run) == 1 else "s",
                                              " ".join(run)))
    return out


CORPUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                      "descriptors.h")


def existing(path=CORPUS):
    """Every descriptor already in the corpus, as name -> bytes.

    Located relative to this file, not the working directory. Reading it as a bare
    relative path meant the duplicate check below silently did nothing whenever the
    tool was run from anywhere but the repo root - it caught nothing and said so by
    exiting 0, which is worse than not having the check.
    """
    try:
        text = open(path).read()
    except OSError as e:
        raise SystemExit(
            "add_descriptor.py: cannot read the corpus at %s (%s).\n"
            "  Refusing to continue: without it the duplicate check would pass "
            "everything." % (path, e.strerror))

    out = {}
    for name, body in re.findall(
            r"static const uint8_t d_(\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        out[name] = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    return out


def duplicate_of(b, corpus):
    """Name of an existing descriptor with exactly these bytes, if any.

    A device dumped from a different vendor can still be byte for byte a descriptor
    already in the corpus - rpi_consumer and cherry_kc6000_consumer were, and it
    went unnoticed because the names looked unrelated. An identical descriptor
    parses identically, so it adds no coverage while inflating every count derived
    from the corpus size.
    """
    for name, bytes_ in corpus.items():
        if bytes_ == b:
            return name
    return None


def sanity(b):
    """Cheap plausibility checks, so a bad paste is caught before it becomes a test."""
    notes = []
    if not b:
        return ["no hex bytes found"]
    if b[0] not in (0x05, 0x06):
        notes.append("does not start with a Usage Page item (05 or 06), check the paste")

    depth, i, collections = 0, 0, 0
    while i < len(b):
        size = b[i] & 0x03
        size = 4 if size == 3 else size
        tag_type = b[i] & 0xFC
        if tag_type == 0xA0:
            depth += 1
            collections += 1
        elif tag_type == 0xC0:
            depth -= 1
        i += 1 + size

    if i != len(b):
        notes.append("items do not land exactly on the end (%d vs %d), descriptor may be "
                     "truncated" % (i, len(b)))
    if depth != 0:
        notes.append("collections unbalanced (%+d), descriptor may be incomplete" % depth)

    # The other three checks can all pass on a descriptor that lost whole lines, as long
    # as it lost the Collection and the End Collection together: the remainder still
    # starts on a Usage Page, still lands on the end, and still balances at depth 0. A
    # descriptor that declares no collection at all declares nothing about the device,
    # so it is the one thing left that catches that paste.
    if collections == 0:
        notes.append("no Collection item at all, so nothing declares what the device is - "
                     "almost always a paste that lost lines")
    return notes


# A keyboard descriptor, 29 bytes, written the several ways a dump arrives. Every
# case below has to come back as exactly this.
KBD = [0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15,
       0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
       0x81, 0x01, 0xC0]

SELFTEST = [
    ("decoded array, comment on every line", KBD, """
0x05, 0x01,   /* Usage Page (Generic Desktop) */
0x09, 0x06,   /* Usage (Keyboard) */
0xA1, 0x01,   /* Collection (Application) */
0x05, 0x07,   /* Usage Page (Keyboard) */
0x19, 0xE0,   /* Usage Minimum (224) */
0x29, 0xE7,   /* Usage Maximum (231) */
0x15, 0x00,   /* Logical Minimum (0) */
0x25, 0x01,   /* Logical Maximum (1) */
0x75, 0x01,   /* Report Size (1) */
0x95, 0x08,   /* Report Count (8) */
0x81, 0x02,   /* Input (Data,Var,Abs) */
0x95, 0x01,   /* Report Count (1) */
0x75, 0x08,   /* Report Size (8) */
0x81, 0x01,   /* Input (Cnst,Arr,Abs) */
0xC0,         /* End Collection */
"""),
    ("decoded array, comments on some lines only", KBD, """
0x05, 0x01,                    /* Usage Page (Generic Desktop) */
0x09, 0x06,                    /* Usage (Keyboard) */
0xA1, 0x01,                    /* Collection (Application) */
0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
0xC0,                          /* End Collection */
"""),
    ("block comment spanning lines", KBD, """
0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
/* the modifier byte, then a byte of padding
   nobody uses, then the six key slots */
0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
0x75, 0x08, 0x81, 0x01, 0xC0,
"""),
    ("usbhid-dump, last line a lone C0", KBD, """
001:004:000:DESCRIPTOR         1719851496.9
 05 01 09 06 A1 01 05 07 19 E0 29 E7 15 00 25 01
 75 01 95 08 81 02 95 01 75 08 81 01
 C0
"""),
    ("// comments", KBD, """
0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,  // usage pages
0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,  // see https://example.com/a
0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
0x75, 0x08, 0x81, 0x01, 0xC0,
"""),
    ("issue body wrapped around a fenced dump", KBD, """
My keyboard is dead ab initio and the ad hoc workaround fails.

```
05 01 09 06 A1 01 05 07 19 E0 29 E7 15 00 25 01
75 01 95 08 81 02 95 01 75 08 81 01 C0
```

Thanks!
"""),
    ("one line", KBD, " ".join("0x%02X," % x for x in KBD)),
    ("prose alone yields nothing", [], "The value was de ad and then be ef, roughly."),
    ("a stray pair of bytes is not a descriptor", [], "notes\n\nde ad\n\nmore notes"),
]


def selftest():
    """Check parse_hex against the dump shapes that have bitten, plus the whole corpus.

    Two of these used to come back short and silent, which is the reason this exists:
    a descriptor that loses its Collection and End Collection together still starts on
    a Usage Page, still lands on the end and still balances, so sanity() saw nothing.
    """
    bad = 0
    for label, want, text in SELFTEST:
        got = parse_hex(text)
        ok = got == want
        bad += not ok
        print("%-4s %-45s %d/%d bytes" % ("ok" if ok else "FAIL", label, len(got),
                                          len(want)))
        if not ok:
            print("       got  %s" % " ".join("%02X" % x for x in got))
            print("       want %s" % " ".join("%02X" % x for x in want))

    # Every real entry has to survive a round trip through the reader, in both the
    # layout this tool prints and the single line a reflowed dump becomes.
    corpus = existing()
    for name, b in corpus.items():
        wide = "\n".join("    " + " ".join("0x%02X," % x for x in b[i:i + 16])
                         for i in range(0, len(b), 16))
        for layout, text in (("16 per line", wide),
                             ("one line", " ".join("0x%02X," % x for x in b))):
            if parse_hex(text) != b:
                print("FAIL %s does not round trip (%s)" % (name, layout))
                bad += 1

    # sanity() has to stay quiet on every descriptor known to be good, or the warnings
    # stop meaning anything.
    for name, b in corpus.items():
        for note in sanity(b):
            print("FAIL %s warns: %s" % (name, note))
            bad += 1

    print("\n%d corpus descriptors round trip and pass sanity" % len(corpus))
    print("%d failed" % bad if bad else "all cases pass")
    return 1 if bad else 0


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    if sys.argv[1] == "--selftest":
        return selftest()

    name = sys.argv[1]
    text = open(sys.argv[2]).read() if len(sys.argv) > 2 else sys.stdin.read()

    notes = []
    b = parse_hex(text, notes)
    notes += sanity(b)

    dup = duplicate_of(b, existing())
    if dup:
        print("refusing to add %s: byte for byte identical to d_%s" % (name, dup),
              file=sys.stderr)
        print("  %d bytes, so it would parse identically and test nothing." % len(b),
              file=sys.stderr)
        print("  If the point is that a second vendor ships the same descriptor, say so",
              file=sys.stderr)
        print("  in a comment on the existing entry rather than adding this one.",
              file=sys.stderr)
        return 1

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
