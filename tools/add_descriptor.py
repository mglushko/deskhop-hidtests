#!/usr/bin/env python3
"""Turn a pasted descriptor dump into a descriptors.h entry.

    add_descriptor.py <name> [file]        # reads stdin if no file
    add_descriptor.py wooting_keyboard dump.txt
    add_descriptor.py --selftest           # check the reader against known dump shapes

Reads usbhid-dump output, the Windows tool's "DESCRIPTOR:" blocks, C arrays with 0x and
commas, decoded arrays with the item in a trailing comment, or bare hex. Non-hex lines
are skipped, so a whole issue comment can be pasted.

Prints the C array and the registry line, and says on stderr what it dropped. It does
not edit descriptors.h: the comment naming the device and issue is yours to write.
"""
import os
import re
import sys

import hiditems

# a hex byte, optionally 0x prefixed, not part of a longer word
TOKEN = re.compile(r"(?:\b0[xX])?([0-9A-Fa-f]{2})\b")

# usbhid-dump prefixes each block with e.g. "001:004:000:DESCRIPTOR  1719851496.9"
NOISE = re.compile(r"DESCRIPTOR|PATH:|^\s*```|bLength|bDescriptorType|^\s*\(\d+ bytes\)")

# what separates bytes in every format worth reading: spaces, commas, 0x prefixes
SEPARATORS = re.compile(r"[\s,]|0[xX]")

# how many bytes a run of hex-only lines needs before it is taken as descriptor data
MIN_BLOCK = 4


def parse_hex(text, notes=None):
    """Every descriptor byte in a pasted dump, in order.

    Lines that are not descriptor data are skipped, and they are judged in runs rather
    than one at a time: a contiguous block of hex-only lines is data if together it
    carries at least MIN_BLOCK bytes, however few any one line holds. That keeps a
    dump's lone final C0 and drops the "de ad" inside a sentence, and a decoded array
    with a C comment on every line is read once the comments are stripped. Both shapes
    used to come back as a short descriptor that passed sanity().

    Anything appended to notes is a run that looked like data and was dropped anyway.
    """
    # C comments first, over the whole text: a pasted array often decodes every item in a
    # trailing comment, and before these were stripped the whole line failed the "nothing
    # but hex" test below and was dropped as prose, silently. hiditems' stripper replaces
    # a comment with a space, so bytes on either side of it cannot fuse.
    text = hiditems.strip_comments(text)

    # tokens for a data line, None for anything that breaks a run
    classified = []
    for raw in text.splitlines():
        if NOISE.search(raw):
            classified.append(None)
            continue

        tokens = TOKEN.findall(raw)
        if not tokens:
            classified.append(None)
            continue

        # a descriptor line is nothing but hex; skip prose that happens to contain "ab"
        if not re.fullmatch(r"[0-9A-Fa-f]*", SEPARATORS.sub("", raw)):
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
    """Every descriptor already in the corpus, as name -> bytes. Located relative to
    this file, not the working directory, so the duplicate check cannot silently pass
    everything when the tool is run from outside the repo root."""
    try:
        text = open(path).read()
    except OSError as e:
        raise SystemExit(
            "add_descriptor.py: cannot read the corpus at %s (%s).\n"
            "  Refusing to continue: without it the duplicate check would pass "
            "everything." % (path, e.strerror))

    return {name[len("d_"):]: b for name, b in hiditems.read_corpus(text).items()}


def duplicate_of(b, corpus):
    """Name of an existing descriptor with exactly these bytes, if any. A device from
    another vendor can be one (rpi_consumer and cherry_kc6000_consumer were): it parses
    identically, so it adds no coverage while inflating every count derived from the
    corpus size."""
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

    depth, end, collections = 0, 0, 0
    for item in hiditems.walk_items(b):
        end = item.end
        if item.tag == 0xA0:
            depth += 1
            collections += 1
        elif item.tag == 0xC0:
            depth -= 1

    if end != len(b):
        notes.append("items do not land exactly on the end (%d vs %d), descriptor may be "
                     "truncated" % (end, len(b)))
    if depth != 0:
        notes.append("collections unbalanced (%+d), descriptor may be incomplete" % depth)

    # The other three checks all pass on a paste that lost a Collection and its End
    # Collection together: the rest still starts on a Usage Page, lands on the end and
    # balances. A descriptor with no collection declares nothing, so this catches it.
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
    Two of the shapes used to come back short and silent; see parse_hex and sanity()."""
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
    if sys.argv[1].startswith("-"):
        raise SystemExit(__doc__)

    name = sys.argv[1]
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        print("add_descriptor.py: '%s' is not a C identifier, and d_%s would not compile"
              % (name, name), file=sys.stderr)
        return 2

    text = open(sys.argv[2]).read() if len(sys.argv) > 2 else sys.stdin.read()

    notes = []
    b = parse_hex(text, notes)
    if not b:
        for note in notes:
            print("WARNING: %s" % note, file=sys.stderr)
        print("add_descriptor.py: no hex bytes found, nothing to add", file=sys.stderr)
        return 1
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
