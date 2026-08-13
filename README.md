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
| `make truncate` | does a short or malformed descriptor make the parser read past the buffer? |
| `make exhaust` | what happens when the usage array runs out before the mouse collection? |
| `make timing` | how long does a large but legal Report Count take to parse? |
| `make all` | build everything without running it |

`compare` is the one to reach for when reviewing a parser change. It materialises the
reference commit with `git archive`, builds the same harness twice, and diffs the
parse of every descriptor. A good fix shows `identical parse` on every known good
device and a difference only on the broken one.

## Adding a device

Most issues arrive with a descriptor dump. Pipe it through the converter, which eats
`usbhid-dump` output, the Windows tool's `DESCRIPTOR:` blocks, C arrays, or bare hex,
and ignores surrounding prose:

```sh
gh issue view 335 --repo hrvach/deskhop --json body --jq .body \
  | python3 tools/add_descriptor.py wooting_keyboard
```

It prints the C array and the registry line, and warns if the items do not land
exactly on the end or the collections are unbalanced, which catches a bad paste
before it becomes a misleading test. Paste both into `descriptors.h` and every
target picks the device up automatically.

Keep captured bytes verbatim, quirks included, and write the device name and issue
number into the comment. Reproducing the quirk is usually the whole point.

### What the corpus is already worth

Two devices added from open issues paid for themselves immediately:

- **Wooting Two HE** ([#335](https://github.com/hrvach/deskhop/issues/335), "only
  CTRL, Shift & Win work"). `make dump D=wooting_keyboard` shows why: the keyboard
  declares four key blocks as separate Usage Min/Max ranges, and `main` keeps only
  the last one. The modifiers survive, every letter key does not. The
  `fix-nkro-multi-block` branch recovers three of the four; the third block is 8 bits
  wide and gets excluded by the `src->size > 32` filter in `hid_report.c`.
- **Cherry KC6000** ([#117](https://github.com/hrvach/deskhop/issues/117), media keys
  not working). A consumer control block with no report ID at all, which is the case
  PR #358 addresses.

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
| `compare` | crashes on `gameball_gesture` and `many_usages` under ASan | no crashes, 12 of 14 identical |
| `mouse` | n/a | 22 of 22 cases |
| `fuzz N=40000` | 113,785,197 out of bounds over 30,316 descriptors, peak index 4564 | 0 out of bounds, peak index 127 |
| `truncate` | 679 of 1106 prefixes overread | 560 of 1106 prefixes overread |
| `exhaust` | crashes | X/Y offsets go to 0 at 127 preceding usages |
| `timing` | n/a | ~17.4 ns/element on x86-64 |

Fuzz counts depend on the generator and the seed. Change either and these move; the
qualitative result, zero versus non-zero, is the part that matters.

## Open findings

None of these are addressed by the #332 fix.

**Short descriptors read past the end of the buffer.** `make truncate` fails on
roughly half of all prefixes, every descriptor, starting at length 1. The parse loop
reads a header and then calls `get_descriptor_value()` for up to four data bytes
without checking they are still inside the buffer:

```c
while (desc_len > 0) {
    item.hdr = *(header_t *)report++;
    item.val = get_descriptor_value(report, item.hdr.size);
```

A one-byte descriptor is enough: the header consumes the only byte, `report` now
points one past the end, and the read happens anyway. `desc_len` then goes negative
and the loop exits, so it is bounded to four bytes, but it is a genuine out of bounds
read driven entirely by device supplied data. Present on `main`, so it predates the
#332 work and belongs in its own issue rather than folded into that PR.

Reproduce the smallest case with:

```sh
make all && ./build/<target>/truncate gameball_trackball 1
```

**The usage cursor never resets across a descriptor.** Visible in `exhaust`: a device
with more than about 126 usages ahead of its pointer collection enumerates without
the cursor moving.

**Report Count is a 32-bit field.** Visible in `timing`: memory stays intact after the
fix, but a large enough count still outruns the 500 ms watchdog.
