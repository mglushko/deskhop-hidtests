# deskhop HID test harness

Host-side tests for [deskhop](https://github.com/hrvach/deskhop)'s HID handling: the
report descriptor parser, the report decoders and the dispatch that picks a receiver
for each report. It compiles the firmware's own `hid_parser.c` and `hid_report.c`,
plus the decode and routing functions that `tools/lift.py` copies verbatim out of
`mouse.c`, `keyboard.c` and `usb.c`, against any checkout, branch or worktree, and runs
them over a corpus of real descriptors and reports under ASan and UBSan.

Nothing here modifies deskhop, and nothing here reimplements it. Results reflect the
real code on the tree you point it at.

## Quick start

```sh
make test                        # did any known good device stop decoding?
make compare REF=main            # did my change alter the parse of any known good device?
make dump D=boot_mouse           # what does the parser make of this descriptor?
make fuzz N=40000                # do generated descriptors stay inside usages[]?
```

`DESKHOP` defaults to `~/deskhop`. Point it at any tree:

```sh
make test DESKHOP=~/deskhop-extended
make compare REF=v0.78 DESKHOP=~/dh-fix
```

Needs `gcc`, `python3` and `make`. No cross compiler. `check-constants` alone wants the
Pico SDK submodule populated in the target tree, and skips rather than fails without it.

## Targets

| target | question it answers |
|---|---|
| `make dump D=<name>` | what offsets, usages and handlers did the parser derive? |
| `make compare REF=<commit>` | does my change alter the parse of any known good device? |
| `make mouse` | do real pointer reports decode to the right X, Y, wheel, pan and buttons? |
| `make kbd` | do real keyboard reports decode to the right modifier and keycodes? |
| `make consumer` | do media and power keys reach the send path? |
| `make dispatch` | which receiver does a report actually reach? |
| `make fuzz N=<n>` | does any generated descriptor push an access outside `usages[]`? |
| `make truncate` | does a short or malformed *descriptor* make the parser read past the buffer? |
| `make shortreport` | does a short *report* make the decode path read past the buffer? |
| `make exhaust` | what happens when the usage array runs out before the mouse collection? |
| `make timing` | how long does a large but legal Report Count take to parse? |
| `make check-constants` | do the constants `harness.h` copies still match TinyUSB's? |
| `make check-parse` | does `add_descriptor.py` still read every dump shape without dropping bytes? |
| `make check-cli` | do the replay tools still answer their command lines as documented? |
| `make test-sleepwake` | does the BOOTSEL emulator debounce and deliver Sleep/Wake safely? |
| `make test` | the regression gate: `mouse`, `kbd`, `consumer`, `check-parse`, `check-cli`, `check-constants`, `test-sleepwake` |
| `make findings` | `fuzz`, `truncate`, `shortreport` and `dispatch`, run for their numbers |
| `make corpus` | regenerate the table in `CORPUS.md` from `descriptors.h` and the case tables |
| `make all` | build everything without running it |

`test` is the gate to wire into CI. It holds only what must pass on any firmware worth
shipping, so a red run means the harness moved or a known good device stopped decoding.
`fuzz`, `truncate`, `shortreport` and `dispatch` stay outside it on purpose: they fail by
design on firmware that has the bug they look for, and their exit status is the finding.
`make findings` runs those four together and reports rather than gates.

`compare` is the one to reach for when reviewing a parser change. It materializes the
reference commit with `git archive`, builds the harness against both trees, and diffs
the parse of every descriptor. A good fix shows `identical parse` on every known good
device and a difference only on the one it targets. `V=1` prints the diffs.

## Adding a device

Most reports arrive as a descriptor dump. The converter reads `usbhid-dump` output,
Windows tool `DESCRIPTOR:` blocks, C arrays with or without decoded comments, or bare
hex, and ignores the prose around them:

```sh
gh issue view <n> --repo hrvach/deskhop --json body --jq .body \
  | python3 tools/add_descriptor.py <name>
```

It prints the C array and the registry line, and warns when the items do not land
exactly on the end, the collections are unbalanced, or there is no collection at all,
which catches a bad paste before it becomes a misleading test. Paste both into
`descriptors.h`, give the entry a row in `tools/corpus_table.py`, and run `make corpus`.
`dump`, `compare`, `truncate`, `fuzz` and `check-parse` pick the device up from there.
The decode targets run hand-written case tables, so a new device tells `mouse`, `kbd`,
`consumer`, `shortreport` and `dispatch` nothing until a case is added in `src/cases_*.h`.

Keep captured bytes verbatim, quirks included, and put the device name and source in
the comment. Reproducing the quirk is usually the point.

### Capturing a descriptor

Dump one interface at a time with a tool that reads the wire. On Linux that is
`usbhid-dump`, which issues the real control transfer:

```sh
sudo usbhid-dump -d 046d:c548
```

From WSL, hand the device over with [usbipd-win][usbipd] first: `usbipd bind --busid <id>`
from an elevated PowerShell, then `usbipd attach --wsl --busid <id>`. The device leaves
Windows while attached, so authenticate `sudo` before attaching if it is the keyboard you
are typing on.

Windows does not expose raw report descriptors to user mode, so a tool on that side
reconstructs one from the parsed caps rather than reading it. Treat such dumps as
approximations: key arrays can vanish into padding, collections can come back empty, and
item encodings can differ from the wire. [CORPUS.md](CORPUS.md) records which tool read
each entry and what the reconstructions got wrong.

### The corpus

| | |
|---|---|
| descriptors in `descriptors.h` | 105 |
| captured from real devices | 93 |
| synthetic probes | 12 |
| declaring a mouse collection | 35 |
| declaring a keyboard collection | 41 |
| declaring consumer control | 36 |
| declaring system control | 25 |
| carrying report IDs | 63 |
| whole interfaces with more than one top-level collection | 40 |
| with hand-written decode cases | mouse 29 devices, keyboard 40, consumer 9 |

[CORPUS.md](CORPUS.md) lists every entry: the device, where the bytes came from, the tool
that read them, and what each has caught.

## How it works

- `include/main.h` and `include/tusb.h` stand in for the real headers, which would drag
  in the whole Pico SDK. Everything the files under test need is in `include/harness.h`.
- The target's `hid_parser.h`, `hid_report.h`, `packet.h`, `protocol.h` and
  `constants.h` are copied verbatim into the build directory, where their quoted
  includes resolve to the shims. The structs under test are always the target's own.
- `tools/lift.py` copies the mouse extractors, the keyboard lookup, the consumer and
  system receivers and, where the target has factored it out, the dispatch decision
  verbatim out of the firmware. A rename upstream breaks the build rather than silently
  testing nothing.
- Where a function cannot be lifted because it reaches `global_state` or the TinyUSB host
  API, the harness records what it hands to the next layer (`src/recorders.c`) or models
  the decision (`src/dispatch.h`), and the run's output says which. Only the lifted form
  is a measurement of the firmware.
- The Makefile detects what a tree can do by grepping for the code that does it, and the
  case tables key their expectations on the resulting `HARNESS_*` flags. A device that
  would take a run down on a tree without the bound it needs is kept out of that run, and
  the last line of the output says so rather than passing silently.
- ASan is on for the correctness targets. Descriptor and report bytes are copied into
  exact-size heap allocations, so a read one byte past the end lands in a redzone and is
  reported rather than returning a neighbor's data. UBSan is on beside it for the
  shift-width class ASan cannot see.
- ASan cannot see an overflow of `usages[]`, because that array sits inside the global
  parser state with other members after it, and ASan puts no redzones between struct
  members. `tools/instrument.py` rewrites every access to the array in a copy of the
  parser to report its absolute index and then clamp, which is what `fuzz` counts.
- `src/cases_mouse.h` and `src/cases_kbd.h` are shared by the decode tests and
  `shortreport`, so one case is checked both for the values it decodes to and for what
  happens when the report is cut short.
- `check-constants` compares the constants hand-copied into `harness.h` against the
  vendored TinyUSB header. A wrong one would not fail to compile; it would shift an
  offset and make every target agree on a wrong answer.
- Build output is keyed to the target's directory name and a hash of its path, so
  switching `DESKHOP` never reuses binaries built against another tree, even one with
  the same name elsewhere.

## Reference numbers

Results against two trees, so a broken harness can be told from a broken firmware.
Taken in September 2026 against upstream `main` at `c220d0c` and
[DeskHop Extended][deskhop-extended] `main` at `637b985`, over the 105-descriptor corpus.
The second column is the tree that runs on hardware, and the one whose regressions cost
something. Where its denominator is larger, the extra rows are devices the harness keeps
out of a run on a tree that lacks the bound they need, and cases that only apply to code
the fork has.

| check | upstream main | [DeskHop Extended][deskhop-extended] |
|---|---|---|
| `compare` | all 105 parse, no crashes | 105 compared against upstream `main`, no crash on either side; differences confined to `ultralink_iface1`, `ultralink_nkro_keyboard` and `keychron_dongle_keyboard`, the three whose key bitmap declares one usage more than it has bits |
| `mouse` | 327 of 327 cases over 28 devices | **327 of 327 over 28**, plus **4 of 4** button fallback cases |
| `kbd` | 165 of 165 cases over 39 devices | **168 of 168 over 40** |
| `consumer` | 29 of 29 over 9 devices | same |
| `dispatch` | 26 of 36 routed correctly, 4 of those only by luck; the 10 misrouted are all boot protocol | **36 of 36**, lifted rather than modeled |
| `check-constants` | all 47 agree with TinyUSB | same |
| `check-parse` | 7 dump shapes read, 2 non-dumps refused, 105 descriptors round trip | same |
| `fuzz N=40000` | 0 out of bounds; lowest index 0, peak 127 | same: the parser is the same file |
| `truncate` | 5069 of 9947 prefixes overread | the same 5069 of 9947 |
| `shortreport` | 1372 of 3352 truncated reports overread | **0 of 3355** |
| `exhaust` | never fails, 10 runs in 10 clean | never fails |
| `timing` | ~18 ns/element on x86-64 | same |

Fuzz counts move with the generator and the seed, `truncate` counts with the size of the
corpus, and `timing` with the host. The qualitative result is what matters: zero against
non-zero, and for `fuzz` the lowest index touched beside the peak, which separates a read
one slot behind the array from a cursor that walked off the end into the thousands.

What the non-zero cells mean is written up in [FINDINGS.md](FINDINGS.md), together with
every other defect this harness has measured, open or fixed, and the confirmations on
hardware.

## What it cannot see

Everything here runs on the host. The harness compiles the firmware's parser, decoders
and routing and feeds them real bytes, which is enough to say what the code does with a
descriptor or a report. It never enumerates a device, never negotiates a protocol and
never sees TinyUSB's host stack, so a finding here is a claim about the firmware, not
about a device on a desk.

Each target also sees only its own layer. `compare` diffs the parse, so a change in a
receiver is invisible to it; `consumer` and `dispatch` exist because changes there needed
a measurement of their own. Read a target's question in the table above before trusting
its silence.

[`emu/`](emu/) closes part of the gap. It turns a spare RP2040 into a stand-in for a
corpus device, with the descriptor generated from `descriptors.h` at build time so the rig
and the corpus cannot drift, and adds a BOOTSEL-driven System Control rig for the
Sleep/Wake path and a [browser page](emu/pointer-bench.html) that shows what a host
actually receives from a pointer. Plugged into a board, an emulator lets a host-side
finding be confirmed or refuted on real silicon.

## License

GPL-3.0, matching deskhop. `tools/lift.py` copies function bodies verbatim out of
deskhop's GPLv3 sources and `tools/instrument.py` writes a modified copy of
`hid_parser.c`, so everything under `build/*/gen/` is a derivative work. Both tools stamp
that notice into what they generate.

[deskhop-extended]: https://github.com/mglushko/deskhop-extended
[usbipd]: https://github.com/dorssel/usbipd-win
