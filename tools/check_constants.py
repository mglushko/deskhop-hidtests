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
import ast
import re
import sys

PREFIXES = ("RI_", "HID_USAGE_", "HID_PROTOCOL_", "HID_ITF_PROTOCOL_")

COMMENTS = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
ENUM_BODY = re.compile(r"\benum\b[^{]*\{(.*?)\}", re.S)
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+([^\n]+?)[ \t]*$", re.M)

INT_SUFFIX = re.compile(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]+\b")
C_OCTAL = re.compile(r"\b0([0-7]+)\b")

# C integer constant expressions, which is all these headers use. Anything outside
# this set evaluates to None and is reported as unresolved rather than skipped.
BINOPS = {
    ast.LShift: lambda a, b: a << b, ast.RShift: lambda a, b: a >> b,
    ast.BitOr: lambda a, b: a | b,   ast.BitAnd: lambda a, b: a & b,
    ast.BitXor: lambda a, b: a ^ b,  ast.Add: lambda a, b: a + b,
    ast.Sub: lambda a, b: a - b,     ast.Mult: lambda a, b: a * b,
}
UNARYOPS = {ast.Invert: lambda a: ~a, ast.USub: lambda a: -a, ast.UAdd: lambda a: +a}


def interesting(name):
    return name.startswith(PREFIXES)


def evaluate(expr, known):
    """Value of a C integer constant expression, or None if it is not one.

    Handles the shapes these headers actually use: literals with U/L suffixes, C
    octal, parenthesised shifts and masks, and references to enumerators already
    seen. Giving up used to mean dropping the constant *and* every implicit
    enumerator after it, while still reporting success - so anything unresolved is
    now surfaced by the caller instead of vanishing.
    """
    expr = INT_SUFFIX.sub(r"\1", expr).strip()
    expr = C_OCTAL.sub(r"0o\1", expr)
    if not expr:
        return None

    try:
        tree = ast.parse(expr, mode="eval")
    except SyntaxError:
        return None

    def walk(node):
        if isinstance(node, ast.Constant):
            return node.value if isinstance(node.value, int) else None
        if isinstance(node, ast.Name):
            return known.get(node.id)
        if isinstance(node, ast.UnaryOp) and type(node.op) in UNARYOPS:
            a = walk(node.operand)
            return None if a is None else UNARYOPS[type(node.op)](a)
        if isinstance(node, ast.BinOp) and type(node.op) in BINOPS:
            a, b = walk(node.left), walk(node.right)
            return None if a is None or b is None else BINOPS[type(node.op)](a, b)
        return None

    return walk(tree.body)


def parse(text):
    """Name -> int for every enumerator and simple #define we care about.

    Enumerators without an explicit initialiser take the previous value plus one,
    exactly as C does. Resolving those matters: TinyUSB writes several of these
    lists with only the first entry numbered, and treating a bare name as absent
    would report a mismatch that is not there.
    """
    text = COMMENTS.sub(" ", text)
    out = {}
    unresolved = set()

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
                name, value = name.strip(), evaluate(val, out)
            else:
                name, value = item, nxt

            if not re.fullmatch(r"[A-Za-z_]\w*", name):
                nxt = None
                continue
            if value is None:
                # Cannot number this one, so the running counter is no longer
                # trustworthy for the rest of the enum either. Record both, so the
                # caller can say what stopped being checked.
                if interesting(name):
                    unresolved.add(name)
                nxt = None
                continue

            out.setdefault(name, value)
            nxt = value + 1

    for name, val in DEFINE.findall(text):
        if name in out:
            continue
        value = evaluate(val, out)
        if value is None:
            if interesting(name):
                unresolved.add(name)
        else:
            out[name] = value

    values = {k: v for k, v in out.items() if interesting(k)}
    return values, unresolved - set(values)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)

    harness_path, tusb_path, sources = sys.argv[1], sys.argv[2], sys.argv[3:]

    harness, harness_unresolved = parse(open(harness_path).read())
    tusb, tusb_unresolved = parse(open(tusb_path).read())

    used, unreadable = set(), []
    for path in sources:
        try:
            used |= set(re.findall(r"\b(?:%s)\w*" % "|".join(PREFIXES), open(path).read()))
        except OSError as e:
            unreadable.append("%s (%s)" % (path, e.strerror))

    mismatched, absent = [], []
    for name in sorted(harness):
        if name not in tusb:
            absent.append(name)
        elif harness[name] != tusb[name]:
            mismatched.append((name, harness[name], tusb[name]))

    # anything harness.h declares that could not be compared, for any reason
    unresolved = sorted(harness_unresolved | (tusb_unresolved & set(harness)))
    unchecked = sorted(set(absent) | set(unresolved))
    checked = len(harness) - len(absent)

    print("  harness.h defines %d constants, %d checked against %s"
          % (len(harness) + len(harness_unresolved), checked, tusb_path))
    if used:
        print("  %d of them are referenced by the parser sources" % len(set(harness) & used))

    if unreadable:
        print("\n  WARNING: could not read, so nothing is marked as parser-used:")
        for p in unreadable:
            print("    %s" % p)

    def flag(name):
        return "  <- used by the parser" if name in used else ""

    if mismatched:
        print("\n  VALUE MISMATCH:")
        for name, h, t in mismatched:
            print("    %-34s harness 0x%02X, tinyusb 0x%02X%s" % (name, h, t, flag(name)))

    if absent:
        print("\n  defined by harness.h but not found in tinyusb hid.h:")
        for name in absent:
            print("    %s%s" % (name, flag(name)))
        print("    (renamed or removed upstream, or defined in another header)")

    if unresolved:
        print("\n  could not be evaluated, so NOT compared:")
        for name in unresolved:
            print("    %s%s" % (name, flag(name)))
        print("    (initialiser is not a constant expression this tool understands)")

    # A constant the parser depends on that went unchecked is a hole in the check,
    # not a note. Reporting "all agree" while quietly covering fewer constants is
    # the failure mode this tool exists to remove.
    critical = [n for n in unchecked if n in used]
    if critical:
        print("\n  RESULT: %d parser-used constant(s) went unchecked: %s"
              % (len(critical), ", ".join(critical)))
        return 1

    if mismatched:
        print("\n  RESULT: harness.h disagrees with TinyUSB, every target is suspect")
        return 1
    if unchecked:
        print("\n  RESULT: %d checked and agreeing, %d NOT checked - see above"
              % (checked, len(unchecked)))
        return 0

    print("\n  RESULT: all %d agree with TinyUSB" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
