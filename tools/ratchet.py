#!/usr/bin/env python3
"""Hold fuzz, truncate and shortreport to a recorded baseline, so their counts may fall
but never rise.

    ratchet.py check  <baseline.tsv> <build dir> <N> <SEED> <tree>
    ratchet.py record <baseline.tsv> <build dir> <N> <SEED> <tree>

The three fail by design on a tree with the bug they look for, so their exit status
cannot gate. What can is the count: one per corpus entry, one per kind of failure, and
fuzz's out-of-bounds totals, each compared against what the baseline recorded for that
tree. A count above its baseline fails, and so does any change of shape: an entry the
baseline lacks or a baseline entry this run lacks (a device that fell behind a HARNESS_*
gate, say), or a denominator that moved. A count below its baseline passes and says so,
since until it is recorded nothing stops it coming back.

`record` writes the baseline from a run; `check` compares against it. Both leave the
tools' full output and the measured table under <build dir>/findings/.
"""
import os
import re
import subprocess
import sys

HEADER = ("check", "entry", "tried", "failed")

# The summary lines each sweep ends its table with, and the kind lines print_tally()
# writes under them (src/support.h).
SUMMARY = {
    "truncate": re.compile(r"^  (\d+) of (\d+) truncations failed$", re.M),
    "shortreport": re.compile(r"^  (\d+) of (\d+) truncated reports failed$", re.M),
}
KIND = re.compile(r"^    +(\d+)  (.+)$")

# One table row: entry, lengths tried, failures, then where the first failure was.
ROW = re.compile(r"^  (\S+) +(\d+) +(\d+) +(?:\d+|case \d+ at \d+ bytes|-)$")

FUZZ = {
    "parsed": re.compile(r"descriptors parsed\s*: (\d+)"),
    "accesses": re.compile(r"out-of-bounds accesses\s*: (\d+)"),
    "descriptors": re.compile(r"descriptors going out of bounds\s*: (\d+)"),
}


def die(msg):
    raise SystemExit("ratchet.py: " + msg)


def run(out, tool, args, saved):
    """Run one sweep, keep its output, and return it. 0 and 1 are the statuses a sweep
    answers with, clean or not; anything else means it did not finish measuring."""
    path = os.path.join(out, tool)
    proc = subprocess.run([path] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = proc.stdout.decode("utf-8", "replace")

    with open(os.path.join(saved, tool + ".txt"), "w") as f:
        f.write(text)
    if proc.returncode not in (0, 1):
        die("%s exited with status %d, so it measured nothing to compare; its output is "
            "in %s" % (tool, proc.returncode, os.path.join(saved, tool + ".txt")))
    return text


def read_fuzz(text, n):
    """Fuzz's two out-of-bounds totals, each over the N descriptors it generated."""
    if "Refusing to report success" in text:
        die("fuzz recorded no access to usages[], so tools/instrument.py no longer matches "
            "this parser; fix that before comparing")

    got = {k: rx.search(text) for k, rx in FUZZ.items()}
    if not all(got.values()):
        die("cannot read fuzz's summary; has its output changed?")
    if int(got["parsed"].group(1)) != n:
        die("fuzz parsed %s descriptors, not the N=%d it was asked for"
            % (got["parsed"].group(1), n))

    return [("fuzz", "descriptors going out of bounds", n, int(got["descriptors"].group(1))),
            ("fuzz", "out-of-bounds accesses", n, int(got["accesses"].group(1)))]


def read_sweep(tool, text):
    """One row per table entry and one per kind of failure. The rows must add up to the
    summary and the kinds to its failures, or the table was misread, and that is refused
    rather than compared."""
    if "were not replayed" in text:
        die("%s skipped cases that its tables got wrong; fix them before comparing" % tool)

    m = SUMMARY[tool].search(text)
    if not m:
        die("cannot find %s's summary line; has its output changed?" % tool)
    failed, total = int(m.group(1)), int(m.group(2))

    head, tail = text[:m.start()], text[m.end():]
    rows = []
    for line in head.splitlines():
        r = ROW.match(line)
        if r:
            rows.append((tool, r.group(1), int(r.group(2)), int(r.group(3))))

    kinds = []
    for line in tail.lstrip("\n").splitlines():
        k = KIND.match(line)
        if not k:
            break
        kinds.append((tool, "kind: " + k.group(2), total, int(k.group(1))))

    if (sum(r[2] for r in rows), sum(r[3] for r in rows)) != (total, failed):
        die("%s's rows add up to %d of %d, its summary says %d of %d; has its output "
            "changed?" % (tool, sum(r[3] for r in rows), sum(r[2] for r in rows), failed,
                          total))
    if sum(k[3] for k in kinds) != failed:
        die("%s's kinds of failure add up to %d, not the %d it counted"
            % (tool, sum(k[3] for k in kinds), failed))
    return rows + kinds


def measure(out, n, seed):
    saved = os.path.join(out, "findings")
    os.makedirs(saved, exist_ok=True)

    measured = read_fuzz(run(out, "fuzz", [str(n), str(seed)], saved), n)
    for tool in ("truncate", "shortreport"):
        measured += read_sweep(tool, run(out, tool, [], saved))
    return measured, saved


def describe_tree(tree):
    """Where the tree came from and at which commit, for the baseline's header. Any
    credentials in the remote URL are dropped rather than written into the repo."""
    def git(*args):
        try:
            return subprocess.run(["git", "-C", tree] + list(args), stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL).stdout.decode().strip()
        except OSError:
            return ""

    sha = git("rev-parse", "--short", "HEAD")
    url = re.sub(r"//[^/@]*@", "//", git("remote", "get-url", "origin"))
    url = re.sub(r"^(https://github\.com/|git@github\.com:)|\.git$", "", url)
    name = url or os.path.basename(os.path.normpath(tree))
    return "%s at %s" % (name, sha) if sha else "%s, not a git checkout" % name


def write_table(path, rows, comments):
    with open(path, "w") as f:
        for c in comments:
            f.write("# %s\n" % c if c else "#\n")
        f.write("\t".join(HEADER) + "\n")
        for r in rows:
            f.write("%s\t%s\t%d\t%d\n" % r)


def read_table(path):
    """The baseline's rows, keyed by (check, entry), and its comment lines."""
    try:
        lines = open(path).read().splitlines()
    except OSError as e:
        die("cannot read the baseline %s (%s); record one with `make baseline "
            "BASELINE=%s`" % (path, e.strerror, path))

    comments = [l[2:] for l in lines if l.startswith("# ")]
    body = [l for l in lines if l and not l.startswith("#")]
    if not body or tuple(body[0].split("\t")) != HEADER:
        die("%s does not start with the %s header" % (path, " / ".join(HEADER)))

    rows = {}
    for l in body[1:]:
        parts = l.split("\t")
        if len(parts) != 4 or not parts[2].isdigit() or not parts[3].isdigit():
            die("%s: cannot read the row %r" % (path, l))
        rows[(parts[0], parts[1])] = (int(parts[2]), int(parts[3]))
    return rows, comments


def annotate(level, msg):
    """Also as a GitHub Actions annotation, so a weekly run's summary says what moved."""
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::%s title=ratchet::%s" % (level, msg.replace("\n", " ")))


def check(baseline, measured):
    base, comments = read_table(baseline)
    now = {(c, e): (t, f) for c, e, t, f in measured}

    worse, improved, shape = [], [], []
    for key, (t, f) in now.items():
        if key not in base:
            shape.append("%s %s: %d of %d failed, and the baseline has no such entry"
                         % (key[0], key[1], f, t))
            continue
        bt, bf = base[key]
        if t != bt:
            shape.append("%s %s: %d tried, the baseline recorded %d" % (key[0], key[1], t, bt))
        elif f > bf:
            worse.append("%s %s: %d of %d failed, the baseline allows %d"
                         % (key[0], key[1], f, t, bf))
        elif f < bf:
            improved.append("%s %s: %d of %d failed, down from %d" % (key[0], key[1], f, t, bf))
    for key, (bt, bf) in base.items():
        if key not in now:
            shape.append("%s %s: in the baseline (%d of %d failed) but not in this run"
                         % (key[0], key[1], bf, bt))

    recorded = next((c for c in comments if c.startswith("tree: ")), "tree: unknown")
    print("ratchet against %s, recorded at %s" % (baseline, recorded[len("tree: "):]))
    for tool in ("fuzz", "truncate", "shortreport"):
        rows = [(k, v) for k, v in now.items() if k[0] == tool]
        if tool == "fuzz":
            for (_, entry), (t, f) in rows:
                print("  %-12s %d %s over %d descriptors" % (tool, f, entry, t))
        else:
            entries = [v for (_, e), v in rows if not e.startswith("kind: ")]
            print("  %-12s %d of %d failed" % (tool, sum(f for _, f in entries),
                                                sum(t for t, _ in entries)))
            for (_, e), (_, f) in rows:
                if e.startswith("kind: "):
                    print("  %-12s   %6d  %s" % ("", f, e[len("kind: "):]))

    for title, items, level in (("WORSE", worse, "error"), ("CHANGED SHAPE", shape, "error"),
                                ("IMPROVED", improved, "warning")):
        if items:
            print("\n  %s:" % title)
            for i in items:
                print("    " + i)
                annotate(level, "%s: %s" % (title.lower(), i))

    if worse:
        print("\n  RESULT: %d count(s) rose above the baseline - a regression" % len(worse))
    if shape:
        print("\n  RESULT: this run and the baseline no longer list the same things. If that "
              "was meant (a corpus\n  or case table edit, or a tree that gained or lost a "
              "fix a probe keys on), record it again:\n    make baseline BASELINE=%s "
              "DESKHOP=<the tree>" % baseline)
    if worse or shape:
        return 1

    if improved:
        print("\n  RESULT: nothing rose, %d count(s) fell. Record them to hold the gain:\n"
              "    make baseline BASELINE=%s DESKHOP=<the tree>" % (len(improved), baseline))
    else:
        print("\n  RESULT: every count matches the baseline")
    return 0


def main():
    if len(sys.argv) != 7 or sys.argv[1] not in ("check", "record"):
        raise SystemExit(__doc__)

    mode, baseline, out, n, seed, tree = sys.argv[1:]
    if not baseline:
        die("name the tree's baseline, e.g. make %s BASELINE=baselines/upstream.tsv"
            % ("ratchet" if mode == "check" else "baseline"))
    try:
        n, seed = int(n), int(seed)
    except ValueError:
        die("N and SEED must be decimal numbers")

    measured, saved = measure(out, n, seed)
    comments = ["Findings baseline for `make ratchet`: what fuzz, truncate and shortreport",
                "measured against one tree. A count may fall but never rise. Written by",
                "`make baseline`, not by hand.",
                "",
                "tree: " + describe_tree(tree),
                "fuzz: N=%d SEED=%d" % (n, seed)]
    write_table(os.path.join(saved, "measured.tsv"), measured, comments)

    if mode == "record":
        os.makedirs(os.path.dirname(os.path.abspath(baseline)), exist_ok=True)
        write_table(baseline, measured, comments)
        print("recorded %d counts from %s in %s" % (len(measured), describe_tree(tree),
                                                   baseline))
        return 0

    return check(baseline, measured)


if __name__ == "__main__":
    sys.exit(main())
