#!/usr/bin/env python3
"""Hold fuzz, truncate and shortreport to a recorded baseline, so their counts may fall
but never rise.

    ratchet.py check  <baseline.tsv> <build dir> <N> <SEED> <tree>
    ratchet.py record <baseline.tsv> <build dir> <N> <SEED> <tree>
    ratchet.py --selftest

The three fail by design on a tree with the bug they look for, so their exit status
cannot gate. What can is the count: one per corpus entry, one per kind of failure, and
fuzz's out-of-bounds totals, each compared against what the baseline recorded for that
tree. A count above its baseline fails, and so does any change of shape: an entry the
baseline lacks or a baseline entry this run lacks (a device that fell behind a HARNESS_*
gate, say), or a denominator that moved. A count below its baseline passes and says so,
since until it is recorded nothing stops it coming back.

A kind of failure is a count and nothing else. A sweep lists a kind only while something
fails that way, so a kind missing on either side counts as zero: a fix that removes the
last failure of a kind is an improvement, and a kind the baseline never saw is a rise.

`record` writes the baseline from a run; `check` reads the baseline first, refuses one
recorded with another N or SEED, then measures and compares. Both leave the tools' full
output and the measured table under <build dir>/findings/. `--selftest` runs the
comparison and the parsing over canned cases; `make test` runs it.
"""
import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile

HEADER = ("check", "entry", "tried", "failed")
KIND_PREFIX = "kind: "

# A sweep takes under a minute here (truncate about 21 s); each forked child also has its
# own time limit (src/support.h). This backstop is for a sweep that stops making progress
# as a whole, so a CI run names the tool rather than timing out silently.
TOOL_TIMEOUT_S = 900

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
    kept = os.path.join(saved, tool + ".txt")
    try:
        proc = subprocess.run([path] + args, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=TOOL_TIMEOUT_S)
    except subprocess.TimeoutExpired as e:
        with open(kept, "wb") as f:
            f.write(e.stdout or b"")
        die("%s did not finish within %d s, so it measured nothing to compare; what it "
            "printed before it was stopped is in %s" % (tool, TOOL_TIMEOUT_S, kept))
    text = proc.stdout.decode("utf-8", "replace")

    with open(kept, "w") as f:
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
        kinds.append((tool, KIND_PREFIX + k.group(2), total, int(k.group(1))))

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
    """The baseline's rows, keyed by (check, entry), and its comment lines. A row listed
    twice is refused: which copy counted would depend on the order, and the looser one
    could let a regression through."""
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
        key = (parts[0], parts[1])
        if key in rows:
            die("%s lists %s %s twice; record it again rather than pick one" % (path, *key))
        rows[key] = (int(parts[2]), int(parts[3]))
    return rows, comments


def recorded_fuzz(comments):
    """The N and SEED the baseline's fuzz counts were measured with, or None."""
    for c in comments:
        m = re.fullmatch(r"fuzz: N=(\d+) SEED=(\d+)", c)
        if m:
            return int(m.group(1)), int(m.group(2))
    return None


def require_same_fuzz(path, comments, n, seed):
    """Fuzz's counts depend on the generator's N and SEED, so a run with others compares
    nothing. Refused before the sweeps run, not scored as a rise or a fall after."""
    got = recorded_fuzz(comments)
    if got is None:
        die("%s does not say which fuzz N and SEED it was recorded with; record it again"
            % path)
    if got != (n, seed):
        die("%s was recorded with fuzz N=%d SEED=%d and this run asks for N=%d SEED=%d; "
            "run with the recorded values, or record a new baseline"
            % (path, got[0], got[1], n, seed))


def annotate(level, msg):
    """Also as a GitHub Actions annotation, so a CI run's summary says what moved."""
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print("::%s title=ratchet::%s" % (level, msg.replace("\n", " ")))


def compare(base, measured, baseline, recorded):
    """Print how measured stands against base, and return 0 if nothing rose and the
    two list the same entries, else 1. Entries are compared on both counts, since a moved
    denominator means the corpus, a case table or a keep-out changed; kinds on failures
    alone, missing on either side read as zero."""
    now = {(c, e): (t, f) for c, e, t, f in measured}
    worse, improved, shape = [], [], []

    for key in sorted(set(now) | set(base)):
        if not key[1].startswith(KIND_PREFIX):
            continue
        f, bf = now.get(key, (0, 0))[1], base.get(key, (0, 0))[1]
        name = "%s %s" % (key[0], key[1])
        if f > bf:
            worse.append("%s: %d, the baseline allows %d" % (name, f, bf)
                         if bf else "%s: %d, a kind the baseline never saw" % (name, f))
        elif f < bf:
            improved.append("%s: %d, down from %d" % (name, f, bf))

    for key, (t, f) in now.items():
        if key[1].startswith(KIND_PREFIX):
            continue
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
        if not key[1].startswith(KIND_PREFIX) and key not in now:
            shape.append("%s %s: in the baseline (%d of %d failed) but not in this run"
                         % (key[0], key[1], bf, bt))

    print("ratchet against %s, recorded at %s" % (baseline, recorded))
    for tool in ("fuzz", "truncate", "shortreport"):
        rows = [(k, v) for k, v in now.items() if k[0] == tool]
        if tool == "fuzz":
            for (_, entry), (t, f) in rows:
                print("  %-12s %d %s over %d descriptors" % (tool, f, entry, t))
        else:
            entries = [v for (_, e), v in rows if not e.startswith(KIND_PREFIX)]
            print("  %-12s %d of %d failed" % (tool, sum(f for _, f in entries),
                                                sum(t for t, _ in entries)))
            for (_, e), (_, f) in rows:
                if e.startswith(KIND_PREFIX):
                    print("  %-12s   %6d  %s" % ("", f, e[len(KIND_PREFIX):]))

    for title, items, level in (("WORSE", worse, "error"), ("CHANGED SHAPE", shape, "error"),
                                ("IMPROVED", improved, "warning")):
        if items:
            print("\n  %s:" % title)
            for i in items:
                print("    " + i)
                annotate(level, "%s: %s" % (title.lower(), i))

    # Advice to re-record sits only beside a change of shape with nothing risen: next to
    # a regression it would read as permission to record the regression.
    if worse:
        print("\n  RESULT: %d count(s) rose above the baseline - a regression" % len(worse))
        if shape:
            print("  The two also list different things (above). Record again only once "
                  "nothing rises.")
    elif shape:
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


# Canned sweep output in the shapes truncate and shortreport print, for the self-test.
CANNED_TRUNCATE = """  DESCRIPTOR                   lengths   failures   first failing length
  -------------------------------------------------------------------------
  boot_mouse                        54         27   1
  many_usages                       40          0   -

  27 of 94 truncations failed
        25  ASan heap-buffer-overflow, read
         2  hung: no result within 10 s

  reproducing the first failure: boot_mouse truncated to 1 bytes
"""
CANNED_SHORTREPORT = """  ENTRY                             lengths   failures   first failing case, length
  ---------------------------------------------------------------------------------
  mouse/boot_mouse/boot                   20         16   case 0 at 1 bytes
  kbd/nkro_keyboard                       25          0   -

  16 of 45 truncated reports failed
        16  ASan heap-buffer-overflow, read
"""


def selftest():
    """The comparison and the parsing over canned cases, each named for the mistake it
    would catch. Needs no build and no tree."""
    K = KIND_PREFIX + "ASan heap-buffer-overflow, read"
    W = KIND_PREFIX + "ASan heap-buffer-overflow, write"
    H = KIND_PREFIX + "hung: no result within 10 s"
    E, F = "mouse/boot_mouse/boot", "out-of-bounds accesses"
    base = {("shortreport", E): (20, 16), ("shortreport", K): (3355, 16),
            ("fuzz", F): (40000, 0)}

    def run_of(failed=16, tried=20, kinds=None, oob=0, extra=()):
        """A measured run like the baseline, with the named parts changed; failed=None
        drops the entry, and kinds lists only the kinds that still fail."""
        kinds = {K: 16} if kinds is None else kinds
        rows = [("shortreport", E, tried, failed)] if failed is not None else []
        rows += [("shortreport", k, 3355, n) for k, n in kinds.items()]
        return rows + [("fuzz", F, 40000, oob)] + list(extra)

    new_entry = [("shortreport", "kbd/new", 8, 0)]
    cases = [
        ("unchanged passes", run_of(), 0),
        ("a partial fix passes", run_of(8, kinds={K: 8}), 0),
        ("a full fix, its kind gone, passes", run_of(0, kinds={}), 0),
        ("a rise in an entry fails", run_of(17, kinds={K: 17}), 1),
        ("a new kind fails", run_of(kinds={K: 15, H: 1}), 1),
        ("a read turning into a write fails", run_of(kinds={W: 16}), 1),
        ("an entry leaving the run fails", run_of(None), 1),
        ("an entry the baseline lacks fails", run_of(extra=new_entry), 1),
        ("a moved denominator fails", run_of(tried=21), 1),
        ("fuzz going out of bounds fails", run_of(oob=3), 1),
    ]

    failures = []
    for name, measured, want in cases:
        with contextlib.redirect_stdout(io.StringIO()) as out:
            got = compare(base, measured, "canned", "canned")
        if got != want:
            failures.append("%s: returned %d, wanted %d\n%s"
                            % (name, got, want, out.getvalue()))

    with contextlib.redirect_stdout(io.StringIO()) as out:
        compare(base, run_of(17, kinds={K: 17}, extra=new_entry), "canned", "canned")
    if "make baseline" in out.getvalue():
        failures.append("a regression beside a shape change still advises re-recording")

    rows = (read_sweep("truncate", CANNED_TRUNCATE)
            + read_sweep("shortreport", CANNED_SHORTREPORT))
    want_rows = [("truncate", "boot_mouse", 54, 27), ("truncate", "many_usages", 40, 0),
                 ("truncate", KIND_PREFIX + "ASan heap-buffer-overflow, read", 94, 25),
                 ("truncate", KIND_PREFIX + "hung: no result within 10 s", 94, 2),
                 ("shortreport", "mouse/boot_mouse/boot", 20, 16),
                 ("shortreport", "kbd/nkro_keyboard", 25, 0),
                 ("shortreport", KIND_PREFIX + "ASan heap-buffer-overflow, read", 45, 16)]
    if rows != want_rows:
        failures.append("canned sweep output read as %r" % (rows,))

    refusals = [
        ("a sweep whose rows do not add up is refused",
         lambda: read_sweep("truncate", CANNED_TRUNCATE.replace(" 27   1", " 26   1"))),
        ("a sweep whose kinds do not add up is refused",
         lambda: read_sweep("truncate",
                            CANNED_TRUNCATE.replace("        25  ", "        24  "))),
    ]
    with tempfile.TemporaryDirectory() as tmp:
        dup = os.path.join(tmp, "dup.tsv")
        write_table(dup, [("truncate", "a", 5, 1), ("truncate", "a", 5, 3)],
                    ["fuzz: N=40000 SEED=1"])
        refusals.append(("a baseline listing a row twice is refused",
                         lambda: read_table(dup)))
        ok = os.path.join(tmp, "ok.tsv")
        write_table(ok, [("truncate", "a", 5, 1)], ["fuzz: N=40000 SEED=1"])
        _, comments = read_table(ok)
        refusals.append(("a baseline recorded with another SEED is refused",
                         lambda: require_same_fuzz(ok, comments, 40000, 7)))
        refusals.append(("a baseline recorded with another N is refused",
                         lambda: require_same_fuzz(ok, comments, 100, 1)))
        refusals.append(("a baseline that does not name its N and SEED is refused",
                         lambda: require_same_fuzz(ok, [], 40000, 1)))
        for name, fn in refusals:
            try:
                fn()
                failures.append("%s: it was accepted" % name)
            except SystemExit:
                pass
        try:
            require_same_fuzz(ok, comments, 40000, 1)
        except SystemExit as e:
            failures.append("the recorded N and SEED were refused: %s" % e)

    total = len(cases) + 2 + len(refusals) + 1
    if failures:
        for f in failures:
            print("  FAIL  " + f)
        print("ratchet: %d of %d self-test cases failed" % (len(failures), total))
        return 1
    print("ratchet: all %d self-test cases pass" % total)
    return 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
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

    # Before the sweeps, not after: a baseline that cannot be compared should say so in
    # a second, not at the end of a minute's measuring.
    if mode == "check":
        base, base_comments = read_table(baseline)
        require_same_fuzz(baseline, base_comments, n, seed)

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

    recorded = next((c[len("tree: "):] for c in base_comments if c.startswith("tree: ")),
                    "an unnamed tree")
    return compare(base, measured, baseline, recorded)


if __name__ == "__main__":
    sys.exit(main())
