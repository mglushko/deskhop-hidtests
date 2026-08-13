# deskhop HID parser test harness

Runs deskhop's `hid_parser.c` and `hid_report.c` on the host, against any checkout,
branch or worktree. Built while chasing [issue #332](https://github.com/hrvach/deskhop/issues/332)
(Gameball trackball taking down a board) and kept because the next HID device issue
will want the same tools.

Nothing here modifies deskhop. It compiles the firmware's own sources and reads its
headers, so results reflect the real code rather than a reimplementation of it.

## Quick start

```sh
make dump D=gameball_trackball          # what does the parser make of this device?
make compare REF=main                   # did my change alter any known good device?
make fuzz N=40000                       # does it stay inside usages[]?
```

`DESKHOP` defaults to `~/deskhop`. Point it anywhere:

```sh
make compare REF=main DESKHOP=~/dh-fix
```

Needs `gcc`, `python3`, and `make`. No cross compiler, no Pico SDK.

## Targets

| target | question it answers |
|---|---|
| `make dump D=<name>` | what offsets, usages and handlers did the parser derive? |
| `make compare REF=<commit>` | does my change alter the parse of any known good device? |
| `make mouse` | do real trackball reports decode to the right X, Y, wheel, pan, buttons? |
| `make fuzz N=<n>` | does any generated descriptor push an access outside `usages[]`? |
| `make exhaust` | what happens when the usage array runs out before the mouse collection? |
| `make timing` | how long does a large but legal Report Count take to parse? |
| `make all` | build everything without running it |

`compare` is the one to reach for when reviewing a parser change. It materialises the
reference commit with `git archive`, builds the same harness twice, and diffs the
parse of every descriptor. A good fix shows `identical parse` on every known good
device and a difference only on the broken one.

## Adding a device

Most issues arrive with a descriptor dump. Paste the bytes into `descriptors.h`, add
the name to the `descriptors[]` table, and every target picks it up automatically.
Keep captured bytes verbatim, quirks included, and note the issue number in the
comment. Reproducing the quirk is usually the whole point.

## How it works

- `include/main.h` and `include/tusb.h` stand in for the real ones, which would drag
  in the whole Pico SDK. Everything the two files under test actually need is in
  `include/harness.h`.
- The target's `hid_parser.h`, `hid_report.h`, `packet.h`, `protocol.h` and
  `constants.h` are copied verbatim into the build dir, where their quoted includes
  resolve to the shims. The structs under test are always the ones from the branch
  being tested, which matters because branches genuinely differ: `main` has a single
  `report_val_t nkro`, the multi-block branch has an array.
- `tools/lift.py` extracts `extract_value`, `extract_report_values` and `get_keyboard`
  verbatim out of `mouse.c` and `keyboard.c`, so the decode path cannot quietly drift
  from what the firmware does. A rename upstream breaks the build rather than
  silently testing nothing.
- `tools/instrument.py` rewrites each `usages[]` access in a copy of the parser to
  report its absolute index and then clamp, which is what lets one process fuzz
  thousands of descriptors that would otherwise corrupt parser state on the first.
- ASan is on for the correctness targets. It is what turns "parses wrong" into an
  exact out-of-bounds report.
- Build output is keyed to the target directory name. Without that, switching
  `DESKHOP` silently reuses binaries built against the previous one, because make
  only compares timestamps and a fresh worktree looks older than the last build.

## Known good numbers

Reference results, so a broken harness is distinguishable from a broken firmware.
Taken against `main` at `59577cc` and the #332 fix at `ea680e4`.

| check | main | fix branch |
|---|---|---|
| `compare` | crashes on `gameball_gesture` and `many_usages` under ASan | no crashes, 10 of 12 identical |
| `mouse` | n/a | 22 of 22 cases |
| `fuzz N=40000` | 113,785,197 out of bounds over 30,316 descriptors, peak index 4564 | 0 out of bounds, peak index 127 |
| `exhaust` | crashes | X/Y offsets go to 0 at 127 preceding usages |
| `timing` | n/a | ~17.4 ns/element on x86-64 |

Fuzz counts depend on the generator and the seed. Change either and these move; the
qualitative result, zero versus non-zero, is the part that matters.

## Two limitations this harness documents

Both are visible in `exhaust` and `timing`, and neither is fixed by the #332 change:

- `p_usage` never resets across a descriptor, so a device with more than about 126
  usages ahead of its pointer collection enumerates without the cursor moving.
- Report Count is a 32-bit field. Memory stays intact after the fix, but a large
  enough count still outruns the 500 ms watchdog.
