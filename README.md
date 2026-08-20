# deskhop HID parser test harness

Runs deskhop's `hid_parser.c` and `hid_report.c` on the host, against any checkout,
branch or worktree. Built while chasing [issue #332][#332] (Gameball trackball taking
down a board) and kept because the next HID device issue will want the same tools.

Nothing here modifies deskhop. It compiles the firmware's own sources and reads its
headers, so results reflect the real code rather than a reimplementation of it.

## Quick start

```sh
make dump D=gameball_trackball          # what does the parser make of this device?
make compare REF=main                   # did my change alter any known good device?
make test                               # did any known good device stop decoding?
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
| `make mouse` | do real pointer reports decode to the right X, Y, wheel, pan, buttons? |
| `make kbd` | do real keyboard reports decode to the right modifier and keycodes? |
| `make consumer` | do media and power keys reach the send path, and does this branch have [#358]? |
| `make fuzz N=<n>` | does any generated descriptor push an access outside `usages[]`? |
| `make truncate` | does a short or malformed *descriptor* make the parser read past the buffer? |
| `make shortreport` | does a short *report* make the decode path read past the buffer? |
| `make dispatch` | which receiver does a report actually reach? |
| `make exhaust` | what happens when the usage array runs out before the mouse collection? |
| `make timing` | how long does a large but legal Report Count take to parse? |
| `make check-constants` | do the constants `harness.h` copies still match TinyUSB's? |
| `make test` | the regression gate: `mouse`, `kbd`, `consumer`, `check-constants` |
| `make findings` | the four bounds checks, run for their numbers |
| `make all` | build everything without running it |

`test` is the one to wire into CI. It holds only the checks that must pass against
any firmware worth shipping, so a red `make test` means the harness moved or a known
good device stopped decoding. `fuzz`, `truncate` and `shortreport` are deliberately
outside it, and so is `dispatch`: they fail by design on firmware that has the bug
they look for - three of the four fail on `main` in the table below - so folding them in
would make the gate permanently red and worth nothing. Their exit status is the finding.
`make findings` runs those four together and reports rather than gates.

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

One trap when pasting: lines carrying fewer than four hex bytes are treated as
prose and skipped, so a dump whose final line is a lone `C0` loses that byte and
then warns about unbalanced collections. Reflow the dump onto one line if the
warning looks wrong.

Keep captured bytes verbatim, quirks included, and write the device name and issue
number into the comment. Reproducing the quirk is usually the whole point.

### Getting bytes from a device you have

Dump one interface at a time, and prefer a tool that reads the wire. On Linux that is
`usbhid-dump`, which issues the real control transfer:

```sh
sudo usbhid-dump -d 046d:c548
```

From WSL, hand the device over with [usbipd-win][usbipd] first - `usbipd bind --busid
<id>` from an elevated PowerShell, then `usbipd attach --wsl --busid <id>`. The device
disappears from Windows while attached, so authenticate `sudo` *before* attaching if you
are dumping the keyboard you are typing on. `--auto-attach` is worth using for devices
that re-enumerate on handover; some do.

Avoid judging a descriptor from [win-hid-dump][winhiddump] output. It reconstructs from
Windows' parsed caps rather than reading the device, and the table in [What the corpus is
already worth](#what-the-corpus-is-already-worth) lists five ways that went wrong on the
two devices dumped both ways - including key arrays vanishing into padding and whole
collections coming back empty. Windows does not expose raw report descriptors to user
mode, so a tool on that side is reconstructing rather than reading; [hidapi's
reconstructor][hidapi] is a closer approximation than HidSharp's if you have no
alternative. Note that `descriptors.h` still carries entries dumped this way from
issues, and the Gameball set shows three artifacts of it. `gameball_keyboard`'s
`95 30 81 03` is padding where a key array should be, and it is not known whether that
device really lacks one. All three of its descriptors encode End Collection as
`0xC1 0x00`, bSize 1 where the spec says 0. And the trackball's X, Y, wheel and pan
carry no Logical Minimum of their own, so the 0 left over from the button block stands
and all four read as unsigned 0..127, a range that discards every negative delta.

The device paths in [#332] settle how they got that way rather than leaving it to be
guessed. They carry Windows' `col01` and `col02` collection suffixes, which is Windows
splitting one interface into a device per collection, a view that exists only in the
parsed caps and never on the wire. So the tool was rebuilding a descriptor from those
caps, which is the failure mode this section already warns about, arrived at from the
other direction.

It is not a theory about the bytes either. Emulating them as posted, a PC enumerates
the device and then suspends it with no driver bound; repairing the End Collection
items makes it enumerate, and giving the axes a signed range makes the pointer travel
in more than one quadrant. `emu/` keeps both states as separate builds so the
comparison stays available. None of this is raised on [#332] itself: it does not change
the fix there, and the reporter did not choose the tool.

### What the corpus is already worth

The corpus is 47 descriptors, 36 of them captured from real devices: 19 from upstream
issues, 4 from dumps published elsewhere, and 13 dumped here from two devices on hand.
What they have bought so far:

- **Wooting Two HE** ([#335], "only CTRL, Shift & Win work"). `make dump
  D=wooting_keyboard` shows why: the keyboard declares four key blocks as separate
  Usage Min/Max ranges, and `main` keeps only the last one. The modifiers survive,
  every letter key does not. PR [#359] recovers all four, the 8-bit block included.
- **Logitech G Pro Superlight 2 receiver** ([#215]) is the same bug wearing a
  different face, on a device that PR was not written for. Its keyboard interface
  declares three key ranges; `main` keeps the *first* rather than the last, because
  only that one clears the `src->size > 32` filter. Letters work, so nothing looks
  broken, but usages 0x87-0x8B and 0x90-0x92 - the Japanese and Korean IME keys - are
  silently dropped. `make compare REF=main` with PR [#359] checked out shows
  `nkro_count=3` where `main` has one block.
- **Cherry MW 8 vs MW 8C** ([#133], "older version worked fine"). The two dumps
  explain the difference in one line of `dump` each: the MW 8 puts buttons, 12-bit
  X/Y and wheel in a single report, the MW 8C splits them across report IDs 1 and 2.
  Both parse and decode correctly on `main` - their 32 cases in `make mouse` all pass -
  so whatever broke for that reporter was fixed by `6c92c11`, which tracks offsets per
  report ID.
- **Cherry KC6000** ([#117], media keys not working). A consumer control block with
  no report ID at all, which is the case PR [#358] addresses - and the only device
  here that separates that PR from `main`. `make consumer` shows what it costs: on
  `main`, pressing Calculator sends Play/Pause, because the receiver reads byte 1
  where the data is in byte 0 and finds bit 0 of the wrong byte set. It does not
  merely lose the key, it reports a different one.
- **8BitDo Retro Mechanical Keyboard** (`2dc8:5201`, [#57], open since March 2024).
  Interface 2 declares three keyboard collections on one interface: a 6KRO keyboard on
  report ID 1 and NKRO bitmaps on 12 and 10. On `main` all three land on `keyboards[0]`,
  which sets `is_nkro` on the entry that also holds the 6KRO key array, so a 6KRO report
  is decoded as though its bytes were bitmap bits. `make kbd` shows `a` coming out as
  keycode 10. It is the sharper version of the Keychron finding below, and the one still
  open upstream.
- **Microsoft Wired Keyboard 600** ([#297]) is the cleanest reproduction of the stale
  usage cursor: its system control block comes out as `usage=0xFF02 page=0x0001`, an
  identifier it never declares, carried over from the vendor block in the preceding
  top-level collection.

The four that did not come from an issue were added to break that selection bias -
every real device above is one that already misbehaved, which is a biased sample.
These are captures published elsewhere, picked for shapes the corpus did not have:

- **Logitech MX518** (`046d:c08e`, from the [tmk_keyboard wiki][tmk]) declares a
  padding item with Report Count 0, puts a two-byte vendor block *inside* the mouse's
  physical collection between the buttons and the axes, and declares the wheel before
  X and Y. All three parse and decode correctly; `make mouse` includes a case asserting
  the vendor bytes never reach an axis.
- **A multi-collection composite** from the [kernel's HID documentation][hidintro] is
  the only descriptor here declaring two mouse collections, on report IDs 1 and 2. See
  the finding below: the second wins and the first goes dark.
- **Raspberry Pi wired keyboard** (`04d9:0006`, from [a gist][rpigist]) bounds its key
  array with a 16-bit `2A FF 00` over the full 0-255 range rather than the usual
  `29 65`, and carries the LED output block a real keyboard has. Its second interface
  is *not* in the corpus: that consumer control block is byte for byte
  `cherry_kc6000_consumer`, so it would parse identically and test nothing. Worth
  knowing when reading [#358] that two unrelated vendors ship the same descriptor,
  but it is not extra coverage.
- **PixArt/HP optical mouse** (`093a:2510`, also from the kernel docs) is a real
  capture of the shape `d_boot_mouse` synthesises, declaring Report Size before Report
  Count and ending in plain `C0`. It confirms item ordering does not change the parse.

The last 13 came from two devices on hand, dumped with `usbhid-dump` after handing each
one to WSL with `usbipd-win`. They are the first entries here that cover a whole USB
interface as the firmware receives it, rather than one collection at a time:

- **Logi Bolt receiver** (`046d:c548`) contributes four interfaces. Interface 1 is the
  richest descriptor in the corpus: mouse, consumer control, system control and a fourth
  collection, on report IDs 2, 3, 4 and 0x0B. Its mouse declares **16 buttons**, more
  than anything else here and enough to reach bit 15 of a signed read - see the button
  finding below. Interface 3 is a Precision Touchpad, the largest descriptor here at 429
  bytes, and the only one using Push and Pop; it parses to nothing at all, which is the
  right answer and is now asserted rather than assumed.
- **Keychron Ultra-Link 8K** (`3434:d028`) contributes five. Interface 1 carries a 6KRO
  keyboard on report ID 7, consumer control on 0x0C, and an NKRO keyboard on 0x11 - and
  it is the entry behind two of the findings below. Both of them are invisible if you
  look at the collections one at a time, which is exactly why the interface-level entries
  exist.

These two also cost the corpus something worth recording. Both were dumped first with
[win-hid-dump][winhiddump], whose HidSharp backend reconstructs descriptors from Windows'
parsed caps rather than reading the wire, and the reconstruction was wrong in ways that
would have produced confident, wrong conclusions:

| what the device declares | what the reconstruction produced |
|---|---|
| a 6-byte key array, `19 00 2A FF 00 ... 81 00` | `95 38 81 03`, constant padding - so no keys at all |
| consumer usages under `05 0C` | 16 constant bits and no usages |
| the Bolt's consumer and system collections | zero bytes, [win-hid-dump issue 2][whd2] |
| HID++ as `81 00` arrays of 6 and 19 bytes | 48 and 152 bits of 1-bit *constant* padding |
| NKRO `2A 98 00` (usage max 152) | `29 97` (usage max 151), which hides the off-by-one |

Every one of those changes what the parser does. Prefer `usbhid-dump` when the bytes
matter; see [Adding a device](#adding-a-device).

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
  exact out-of-bounds report - **for descriptor and report bytes**, which are heap
  allocated at exactly the right size by `truncate`, `shortreport` and the decode
  tests so a redzone sits immediately after the last valid byte.
- UBSan is on beside it, for a class ASan cannot see. `get_report_value()` computes
  `(1u << val->size) - 1` and `0xFFFFFFFFU << val->size`, and `val->size` is the
  *swapped* Report Count for 1-bit fields, so a mouse declaring 40 one-bit buttons
  shifts by 40. That is undefined, and on x86 it silently takes the shift mod 32 and
  returns a plausible wrong number rather than faulting. No device in the corpus
  reaches that path, so this costs nothing and is waiting.
- The decode tests key their expectations on what the target can do, detected by the
  Makefile rather than declared by the target. `MAX_NKRO_BLOCKS` used to be enough on its
  own, but stopped being once more than one fix existed: [#359] defines it and so does
  everything built on top, including trees without the multi-keyboard fix. So
  `HARNESS_MULTI_KEYBOARD` is set when the target has `get_or_add_keyboard`, and
  `HARNESS_BOUNDED_BITMAP` when its `extract_bit_variable` is bounded against the report
  length. The second gates one device out rather than changing an answer: without the
  bound, `bitdo_retro_iface2` reads off the end and takes the whole run down with it.
- `src/dispatch.h` models the routing half of `usb.c:tuh_hid_report_received_cb` -
  which receiver a report reaches, given the interface and the bytes. It cannot be
  lifted (the real function reaches `global_state` and the TinyUSB host API), so it is
  the one piece of firmware logic here that is reimplemented, and it carries a
  "re-check against usb.c" note saying so. `mousetest` and `dispatchtest` share it, so
  a drift shows up in one place rather than two.
- `src/cases_mouse.h`, `src/cases_kbd.h` and `src/cases_cc.h` hold the decode cases,
  shared by `mousetest`/`kbdtest` and `shortreport`. A case added there is checked
  both for the values it decodes to and for its behaviour when truncated, without
  being written twice.
- `dump` prints `cc_array` and `sys_array` under the `consumer:` and `system:`
  sections rather than under the keyboard loop. They live on a `keyboard_t`, but they
  are consumer and system state - `handle_consumer_control_values()` writes them
  through `get_keyboard()` and `process_consumer_report()` reads them back the same
  way. Printed under the keyboard loop they vanished whenever `num_keyboards` was 0,
  which is exactly `cherry_kc6000_consumer`, the one device whose `cc_array` decides
  whether [#358] does anything.
- **ASan cannot see the `usages[]` overflow at all**, which is the whole reason
  `tools/instrument.py` and `fuzz` exist. `usages[128]` is a member of the global
  `parser_state_t`, followed immediately by `p_usage`, `global_usage`, `collection`,
  `report_offsets[]`, `globals[]` and `locals[]`. A modest overrun therefore stays
  *inside* one object, and ASan does not put redzones between struct members - so it
  is not merely hard to catch, it is invisible by construction. What it corrupts
  first is `p_usage` itself, the cursor that caused the overrun. A large enough
  overrun leaves the object entirely and becomes a wild write, which is the
  nondeterministic `exhaust` crash described below. Neither shape is something ASan
  will report, which is why the bounds question is answered by counting instrumented
  accesses instead.
- `make check-constants` compares `harness.h`'s hand-copied TinyUSB constants against
  the vendored header. Nothing in a normal build checks them, and a wrong one would
  not fail to compile - it would shift an offset and make every target agree on a
  wrong answer.
- Build output is keyed to the target directory name. Without that, switching
  `DESKHOP` silently reuses binaries built against the previous one, because make
  only compares timestamps and a fresh worktree looks older than the last build.

## Known good numbers

Reference results, so a broken harness is distinguishable from a broken firmware.
Taken against `main` at `59577cc`, the [#332] fix (now PR [#361]) at `ea680e4`, and
[DeskHop Extended][deskhop-extended], over the current 47-descriptor corpus.

The third column is the one that matters day to day. `main` and [#361] are references;
DeskHop Extended carries all three upstream PRs plus the short-report, boot-routing and
multi-keyboard fixes, and is what actually runs on the hardware, so it is the tree whose
regressions cost something.

Two rows have a larger denominator there rather than a comparable count.
`bitdo_retro_iface2` only enters `kbd` and `shortreport` on a tree that bounds the bitmap
walk, because without the bound that device reads off the end and takes the run down.

`truncate` is the one row where the fork is no better than [#361], and deliberately so:
that is the short *descriptor* finding in the parse loop, which nothing here has fixed. It
is easy to mistake for the short *report* row above it, which is why the findings below
separate the two.

| check | main | [#361] | [DeskHop Extended][deskhop-extended] |
|---|---|---|---|
| `compare` | crashes on `gameball_gesture` and `many_usages` under ASan | both crashes fixed, other 45 identical | both fixed; 47 compared, differences confined to keyboards |
| `mouse` | 124 of 124 cases over 10 devices | 124 of 124 cases | 124 of 124 |
| `kbd` | 49 of 49 cases over 14 devices | same - #361 is a parser change | **55 of 55 over 15** |
| `consumer` | 18 of 18 over 5 devices; verdict "does NOT have the #358 fix" | same - #361 is a parser change | 18 of 18; verdict "has the #358 fix" |
| `dispatch` | 14 of 22 routed correctly; 8 misrouted, 4 more arrive by luck | same - `usb.c` is untouched by these PRs | **22 of 22**, lifted rather than modelled |
| `check-constants` | all 47 agree with TinyUSB | same | same |
| `fuzz N=40000` | 113,785,197 out of bounds over 30,316 descriptors, peak index 4564 | 0 out of bounds, peak index 127 | 0 out of bounds, peak index 127 |
| `truncate` | 2321 of 4337 prefixes overread | 2202 of 4337 prefixes overread | 2202 of 4337 - still open, see below |
| `shortreport` | 779 of 1169 truncated reports overread | 779 of 1169, identical - the fix is in the parser, this is the decode path | **0 of 1185** |
| `exhaust` | fails 10 runs in 10 under the sanitisers, see below | never fails; Y offset goes to 0 at 126 preceding usages, X at 127, and stays there | never fails |
| `timing` | segfaults | ~17.5 ns/element on x86-64 | ~17.4 ns/element |

Fuzz counts depend on the generator and the seed, and `truncate` counts move with
the size of the corpus. Change either and these numbers move; the qualitative
result, zero versus non-zero, is the part that matters. The `timing` figure is per
host, not per firmware.

`exhaust` on `main` used to be the one entry here that was not reproducible. At 500
preceding usages `p_usage` has walked clean out of `parser_state`, and this line
writes through it:

```c
*parser->p_usage = *(parser->p_usage - parser->usage_count);
```

Where that write lands is decided by the process memory map, so the same binary
segfaulted on some runs and printed a plausible-looking table on others - measured at
8 crashes in 10 with ASLR on, and 0 in 10 under `setarch -R`. That warning is now
obsolete, and turning ASan and UBSan on together is what obsoleted it: **`main` now
fails 10 runs in 10**, with a diagnosis rather than a signal. Which sanitiser catches
it first still moves with the layout - measured at 6 runs reporting ASan
`global-buffer-overflow` in `store_element` (`hid_parser.c:77`, the read of
`*(parser->p_usage + i)`, which ASan calls "a wild pointer") and 4 reporting a UBSan
misaligned `uint16_t` store at `hid_parser.c:104` - but a clean run is no longer one
of the outcomes. Both are the same root cause: a cursor that has left the object.

The write itself is still layout-dependent, so this says the sanitisers now catch it
reliably on this build, not that the underlying behaviour became deterministic.

The corpus size feeds into this too, though not by moving what sits next to
`parser_state`: the descriptor arrays are `static const` with initialisers, so
they land in `.rodata`, and what actually follows `parser_state` in the BSS is
`exhaust.c`'s own `iface` and `desc[16384]`, by link order. What changing the
corpus moves is the size of `.rodata`, and so where the BSS lands relative to
page boundaries and the heap - which is enough, because the write has already
left the object and its landing site is decided by the process memory map.
Removing one unrelated descriptor was once enough to change this from "usually prints
garbage" to "usually segfaults". It now only changes which sanitiser reports it.

The other two open parser PRs, measured the same way:

| PR | what `compare REF=main` shows |
|---|---|
| [#359] keep all key sections | every keyboard parses differently, as it must; `wooting_keyboard` gains all four blocks and `superlight2_rx_keyboard` all three. Nothing else in the corpus moves. `make kbd` carries this the rest of the way: on `main`, holding shift and `a` on the Wooting yields modifier `0x02` and no keycode, and on this branch the same bytes yield modifier `0x02` and keycode 4. |
| [#358] media keys without report IDs | identical parse on all 46, including `cherry_kc6000_consumer`, the device it fixes - which is the point, and why `make consumer` exists. That target classifies it correctly: 7 separating rows, verdict "this branch has the #358 fix". Every report-ID device is unchanged. |

Three caveats on [#359], of which one is fixed and two stand.

**Fixed.** [#359] set `is_nkro` on the strength of any single block matching
`maps_usage_per_bit`, and `kbd_with_bit_field` is the case that found it. A plain
keyboard-page bit field matches that test - eight bits of F13
to F20 where the reserved byte usually sits is enough. That routes every report
through `_extract_kbd_nkro`, which never reads `key_array`, so an ordinary 6KRO
keyboard keeps its modifiers and loses every keycode. `main` is unaffected because its
`size > 32` filter ignores a block that small. Deciding `is_nkro` on the summed width
of all blocks keeps both: eight bits is padding, the Wooting's four ranges are not.
Holding `a` on that device returns nothing before the fix and keycode 4 after, with
the rest of the corpus parsing identically.

**Still open, and the reason the count above is three rather than one.**
`MAX_NKRO_BLOCKS` is 4 and the Wooting declares exactly 4, so there is no headroom: a
keyboard splitting its bitmap five ways still loses the last section, silently and in
the same way. Deciding `is_nkro` on the summed width does not change that - the fifth
block is never recorded, so its bits are not in the sum either.

**Also still open**, and sharper, because it is a case where the new rule rejects
something `main` accepted. [#359] recognises an NKRO block by `maps_usage_per_bit`, requiring
`usage_max - usage_min + 1 == size`. The Keychron declares `19 00 2A 98 00` with `95 98`
- usage minimum 0, usage maximum 152, 152 bits, which is 153 usages mapped onto 152 bits
- so the block is never recorded and `nkro_count` stays 0. `main` does record it, on
`size > 32`, and then throws it away at decode time when `_extract_kbd_nkro` applies the
identical 1:1 test. Both branches end up in `_extract_kbd_other` and lose every key, so
this is not a regression, but it does mean the stricter rule silently drops a real
keyboard's bitmap at parse time rather than decode time. Whether a descriptor that is off
by one should be honoured or rejected is a judgement call worth making deliberately.

[#358] changes two functions, and in practice only one of them matters. Its consumer
half fixes a real device, the Cherry KC6000. Its system half needs an interface with a
System Control collection and no report ID anywhere on it, and no descriptor in the
deskhop issue tracker has one: all 220 issues were searched on 2026-08-19, yielding 150
unique interface descriptors, and all nine carrying a system collection declare a report
ID. The reason looks structural rather than accidental: all nine sit on an interface
shared with Consumer Control, and once two collections share an interface, report IDs
are what tell them apart. The nearest real miss is `cherry_mw8c_consumer`, whose system
collection declares no ID *of its own* - but its interface still uses IDs from the
consumer block ahead of it, so `uses_report_id` is set and the PR is inert on it.

That is why `d_system_no_report_id` is synthetic and says so. The path is real in the
code and absent from the field, and `make consumer` exercises it so the system half is
measured rather than assumed - but that row is a code path, not a device.

The two new devices add nothing for [#358]: the Bolt's consumer and system collections
both declare their own report IDs, which is precisely the case that PR does *not* change.
Those bytes were not in the corpus before, so this is now measured rather than assumed.

## Open findings

None of these are addressed by the [#332] fix.

**Short reports read past the end of the buffer, in four separate places.** This
is the counterpart to the truncated-descriptor finding below, and the more serious
of the two: a descriptor arrives once at enumeration, a report arrives thousands of
times a second, and nothing checks either against the length the other implied. A
descriptor can declare a 30-byte NKRO bitmap and the device can then send eight
bytes; every offset the parser derived now points past the end.

`make shortreport` replays each `mouse` and `kbd` case at every length from its
receiver's floor up to full, in an exact-size allocation, forked, under ASan. On
`main` 779 of 1169 fail. The four distinct causes, each reproducible on its own:

```sh
make shortreport                          # the table
./build/<target>/shortreport mx518_mouse 0 5        # 1
./build/<target>/shortreport kensington_expert_mouse 0 1  # 2
./build/<target>/shortreport nkro_keyboard 0 8      # 3
./build/<target>/shortreport boot_mouse 0 1         # 4
```

1. **`get_report_value()` reads `report[len]`.** The loop tests `byte_offset`
   *before* incrementing it:

   ```c
   while (val->size > remaining_bits && byte_offset < len) {
       result |= report[++byte_offset] << remaining_bits;
   ```

   so a field still needing bits when `byte_offset == len - 1` reads one past the
   end. `mx518_mouse` shows it alone, because that device declares no report ID
   and so cannot be hitting cause 2 as well.

2. **`extract_value()` passes a `len` it has already invalidated.** It steps
   `raw_report` past the report ID byte and then hands `get_report_value()` the
   *original* length, so the `byte_offset >= len` guard is off by one in the
   shifted frame - and stacks with cause 1 for up to two bytes past the end.
   Every report-ID device in the corpus fails at length 1 for this reason.

3. **`extract_bit_variable()` has no bound on the report buffer at all.** Its loop
   is bounded by `key_count < len` where `len` is `KEYS_IN_USB_REPORT`, six - the
   number of keys *found*, not the size of the buffer. It walks
   `usage_max - usage_min + 1` bits regardless, which is 240 on `nkro_keyboard`, so
   an 8-byte report is read to byte index 30. `_extract_kbd_other` is the same
   shape, copying `src[i]` for every `i < MAX_KEYS` that `key_array` marks.

4. **The boot-protocol path casts without checking length.**
   `extract_report_values()` returns early when the protocol is BOOT and reads
   through a 5-byte `hid_mouse_report_t *` without consulting `len`. `mousetest`
   had no boot-protocol mouse case until now, which is why this had not come up;
   `kbdtest` has had the keyboard equivalent all along.

Reachability differs per path, because each receiver applies its own guard before
the decode path, and `shortreport` starts at that floor rather than at 1 so it
cannot report something a device is unable to send:

| receiver | guard | shortest reachable report |
|---|---|---|
| `process_mouse_report` | none | 1 byte |
| `process_keyboard_report` | `length < KBD_REPORT_LENGTH` returns | 8 bytes |

Those floors are hand copies of firmware logic, like the routing in `src/dispatch.h`,
and unlike that one they are load bearing - raising a floor hides a finding and
lowering one invents a false one. They are commented as such in `src/shortreport.c`.

Present identically on `main` and on [#361]: 779 of 1169 either way. The [#332]
work is in the parser, and all four of these are in the decode path.

**A keyboard in boot protocol is routed by its modifier byte.** `usb.c` picks between
two branches on `iface->uses_report_id`, which the parser sets from the *descriptor* at
enumeration and nothing ever revises. It never looks at `iface->protocol`. So when
`force_kbd_boot_protocol` puts a keyboard into boot protocol the device stops sending a
report ID, but dispatch keeps reading `report[0]` as one - and `report[0]` is now the
modifier byte:

```c
if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
    uint8_t report_id = 0;
    if (iface->uses_report_id)
        report_id = report[0];
    ...
        process_report_f receiver = iface->report_handler[report_id];
```

On a keyboard declaring no report ID this is harmless - the `else if` below picks the
receiver from the interface protocol. On one that does declare a report ID, the report
goes to `report_handler[modifier]`, and `make dispatch` measures what that means on the
three real devices in the corpus that are affected:

| device | handlers | result |
|---|---|---|
| Gameball `0782:001B` | `.K` | no modifier held - `handler[0]` is NULL, keystroke discarded |
| Keychron Ultra-Link `3434:D028` | `.......K` | only modifier `0x07` routes at all |
| Superlight 2 receiver | `.K.CS` | Ctrl+Shift reaches `process_consumer_report`; **Alt reaches `process_system_report`** |

The last row is the worst: a keyboard report arrives at the path that sends Power and
Sleep. And `extract_kbd_data`'s `HID_PROTOCOL_BOOT` branch is unreachable for every
keyboard that declares a report ID - the branch exists for exactly these devices.

The mouse side has the same mistake, reached through `force_mouse_boot_mode` instead:
there `report[0]` is the button byte, so a boot-mode mouse on a report-ID interface is
routed by which buttons are held.

**That half is confirmed on hardware.** The Keychron Ultra-Link 8K (`3434:D028`)
presents its interface 0 as `Class_03 SubClass_01 Prot_02`, a boot-capable mouse, and
its pointer sits on report ID 1. Ticking Force Mouse Boot Mode on DeskHop Extended
v1.03 and replugging the dongle gives exactly what the handler map predicts: the
cursor is dead, and moves only while the left button is held, because button 1 sets
bit 0 and report ID 1 is the one bound handler. Every other button value routes to an
unbound slot and the report is discarded.

Two things that matter about that. It is the first finding here confirmed on a device
rather than in the harness, and the harness predicted the symptom exactly before
anyone plugged anything in - `make dispatch` had the row as `(dropped)` against
`want mouse`. And it moves this off the "opt-in, therefore theoretical" shelf: the
setting is a checkbox on the config page, and anyone who ticks it with a report-ID
mouse loses the pointer.

**The fix is confirmed on the same hardware.** With the routing change flashed and the
box still ticked, the pointer moves normally. One thing to expect and not mistake for a
regression: the scroll wheel stops working in boot mode. That is boot protocol, not the
fix - the boot mouse report is three bytes, buttons/X/Y, with no wheel in it, and
deskhop says so itself in the branch that prefers report protocol ("looking at you,
mouse wheel"). Untick the box and the wheel returns. Before `fe908d0` the boot branch
read the wheel byte anyway, off the end of a three-byte report and into TinyUSB's shared
endpoint buffer, so whatever scrolling that produced was stale bytes rather than the
wheel.

The keyboard half is still harness-only. The same dongle's interface 1 is
`SubClass_01 Prot_01`, a boot keyboard with report ID 7 - the right shape - but has no
keyboard paired to it, so no keystrokes flow.

Three rows in `make dispatch` pass *by luck*, flagged as such. The target does not have
to be asked which routing it uses to know that - in boot protocol `report[0]` is data,
so routing must not depend on it, and the test simply perturbs that byte and sees
whether the receiver moves. Left Ctrl on a report-ID-1 keyboard is the common case.

**How `make dispatch` knows.** `tuh_hid_report_received_cb` cannot be lifted - it reaches
`global_state` and the TinyUSB host API - but the decision inside it is a pure function
of the interface, the interface protocol and `report[0]`. A target that factors that out
as `pick_receiver()` gets it lifted verbatim like everything else here, and the run says
`routing: lifted from the target's usb.c`. A target that still has it inlined in the
callback has nothing to lift, so `src/dispatch.h`'s model stands in and the run says
`MODELLED` instead. Only the first is a measurement of the firmware; the second reports
what the model was written to say, which is exactly how this bug survived in the first
place - `mousetest` carried a copy of these rules, display-only and documented as able to
go stale, and it duly kept printing the old answer.

`usb.c` is byte for byte the same on `main`, on all three PRs and on DeskHop Extended,
so this is upstream's and long-standing rather than anything a fix introduced. Both
flags default to 0, so it is opt-in - but both are checkboxes on the config page, and
the mouse one has now been ticked on real hardware with the predicted result. [#229] reports keys dying with that option enabled, which is *consistent*
with this - but that reporter's Wooting declares no report ID on its keyboard interface,
so treat the link as suggestive rather than established.

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
[#332] work and belongs in its own issue rather than folded into that PR.

This is **not** the same finding as the short-report one above, and the two are easy to
conflate because both show up as truncation. That one is about the *report*, fixed in
`hid_report.c` and `mouse.c`; this one is about the *descriptor*, in the parse loop, and
is still open everywhere - `make truncate` fails on roughly half of all prefixes even on
a tree carrying every fix measured here. The count is in the table above rather than
repeated here, because repeating it is how this sentence came to quote a corpus one
device out of date.

Reproduce the smallest case with:

```sh
make all && ./build/<target>/truncate gameball_trackball 1
```

**The usage cursor never resets across a descriptor.** Visible in `exhaust`: a device
with more than about 126 usages ahead of its pointer collection enumerates without
the cursor moving. It is also visible on a shipping keyboard, which is the easier
case to argue from:

```sh
make dump D=ms600_consumer     # system: usage=0xFF02 page=0x0001
```

The Microsoft 600's system control collection declares a usage range (`19 00 29 FF`)
and no single usage of its own, and comes out carrying `0xFF02`, the last usage named
by the *vendor* block in the previous top-level collection.

**A collection nested inside another Application collection is lost.** The Cherry MW 8C's
second interface wraps its whole descriptor in one Application collection and opens three
more inside it: a consumer array, a system control block and a vendor page.

`IS_BLOCK_END` means depth zero, and `handle_local_item()` promotes a `Usage` to
`global_usage` only there, so the `Usage (System Control)` naming the second block sits at
depth 1 and never becomes the global usage. It stays Consumer Control for the rest of the
descriptor. The system elements then reach `extract_data()` carrying `global_usage` 0x01
where the map row wants 0x80, nothing matches, and no ID, handler or receiver is recorded.
`make dump D=cherry_mw8c_consumer` finds the consumer block and no system block at all:
`handlers:.C`, `system: rid=0`. Power and sleep from that keyboard can never arrive,
whatever the consumer fix does.

All three collections do share report ID 1, because the ID is declared once in the outer
collection before any of them opens, but that is incidental rather than causal - the block
would be dropped the same way if it declared an ID of its own.

**Fixing it exposes a second defect rather than finishing the job.**
`iface->report_handler[val->report_id] = hay->receiver` is unconditional and there is one
slot per ID, so once the system block matched it would overwrite the consumer's binding on
report 1: power and sleep would start working and the media keys would stop. One report ID
carrying two collections is something the routing cannot currently express, so the
recognition fix alone is a net loss on the only device that wants it.

The reach is that one device. 18 of the 47 descriptors here have a collection whose naming
`Usage` sits at depth 1 or deeper, but 17 of them are the ordinary `Usage (Pointer)`
opening a Physical collection inside a mouse, where leaving `global_usage` alone is
correct. Only this one nests Application inside Application, so promoting the usage at any
depth would break the other 17.

Not previously reported, and separate from [#358].

**Report Count is a 32-bit field.** Visible in `timing`: memory stays intact after the
fix, but a large enough count still outruns the 500 ms watchdog.

**A second mouse collection blanks the first.** `kernel_multi_collection` declares two,
on report IDs 1 and 2, identically laid out. The parser walks both, and the second
overwrites `mouse.report_id` with 2. `extract_value()` opens by rejecting any report
whose leading ID byte does not match:

```c
if (uses_id && (*raw_report++ != src->report_id))
    return false;
```

so every field of a report ID 1 packet fails, and it decodes to all zeros - while
`report_handler[1]` still points at `process_mouse_report`, bound while the first
collection was being parsed. The report is routed to the mouse path and then silently
dropped there. Reproduce with `make mouse`, last case of that device.

Nothing is lost on this particular device, because both collections declare the same
layout, so decoding ID 1 with ID 2's offsets would have given the right answer anyway.
But an interface holds one `mouse_t`, so a device whose two collections disagreed would
have no way to say so. Distinct from the Cherry MW 8C finding above, which is about a
collection that declares no report ID at all rather than two that each declare one.

**Two keyboard collections on one interface collapse into one, and the second corrupts
the first.** The Keychron Ultra-Link 8K puts a 6KRO keyboard on report ID 7 and an NKRO
keyboard on report ID 0x11 on the same interface. `get_keyboard()` short-circuits:

```c
if (iface->num_keyboards == 1 || !iface->uses_report_id)
    return &iface->keyboards[PRIMARY_KEYBOARD];
```

so once the first keyboard is registered every later lookup returns keyboards[0] again.
`handle_keyboard_descriptor_values()` only increments `num_keyboards` when
`!keyboard->is_found`, and it is always handed keyboards[0], so **`num_keyboards` can
never exceed 1 on an interface that uses report IDs** and `MAX_KEYBOARDS` (5) is
unreachable. `get_next_keyboard_id()` does write `keyboards[1].report_id = 0x11`, but
nothing else ever reaches that slot.

The damage is not just that the second keyboard is lost. Its NKRO block sits at
`offset_idx` 1, and this line runs unconditionally:

```c
if (src->offset_idx < MAX_KEYS)
    keyboard->key_array[src->offset_idx] = (src->data_type == ARRAY);
```

The NKRO block is VARIABLE, so that assignment *clears* the `key_array[1]` the report ID
7 collection had set - byte 1 being the first of that keyboard's six key slots. The
result is that the first keycode in every 6KRO report is silently dropped: hold `a` alone
and nothing comes out. Present identically on `main` and on all three PR branches.

```sh
make dump D=ultralink_keyboard   # keys=01111110, alone and correct
make dump D=ultralink_iface1     # keys=00111110, slot 1 cleared
make kbd                         # the decode consequence, both entries side by side
```

The isolated collection and the whole interface are both in the corpus precisely so the
two can be compared. Distinct from the two mouse findings above: this is two collections
that each declare a report ID and still end up sharing one `keyboard_t`.

`bitdo_retro_iface2` ([#57], `2dc8:5201`) is the same fault with a worse ending. It puts
three keyboard collections on one interface, and both of its NKRO blocks map one usage per
bit over 120 bits, so they pass every test the parser applies. Landing on `keyboards[0]`
they set `is_nkro` on the entry that also carries the 6KRO key array, and
`_extract_kbd_nkro` then runs for report ID 1 as well - a 6KRO report decoded as bitmap
bits. Holding `a` produces keycode 10. On `main` it is worse still: the 120-bit walk runs
off the end of the 9-byte report, which is the unbounded `extract_bit_variable` finding
above showing up through this one. That is why `make kbd` only carries this device on a
tree that bounds the walk, and why `truncate` and `shortreport` are where the overread
itself is counted.

**Fixed in DeskHop Extended.** `get_keyboard()` is now lookup only and a parse-time
`get_or_add_keyboard()` claims a slot per report ID, so an interface holds one
`keyboard_t` per collection:

```sh
make dump D=ultralink_iface1 DESKHOP=~/deskhop-extended   # keyboards: 2, keys=01111110
make dump D=bitdo_retro_iface2 DESKHOP=~/deskhop-extended # keyboards: 3, kbd[0] nkro=0
```

`kbd[0]` then matches `ultralink_keyboard`, the same collection parsed on its own, which
is the comparison those two entries were added for. `make kbd` goes to 55 of 55 over 15
devices, and the three upstream reports that share this shape - [#57], [#211] and [#295],
one still open, one closed and reported as returning, one closed by the reporter
hard-coding a workaround for their own keyboard - are all this fault.

**Push and Pop are ignored.** `bolt_rx_touchpad` is the first descriptor here to use
`A4`/`B4`. `handle_global_item()` stores every global by tag and has no case for either,
so `RI_GLOBAL_PUSH` and `RI_GLOBAL_POP` land in `globals[10]` and `globals[11]` and the
global item state is never saved or restored. Benign on this device - each finger
collection re-declares its own Report Size, Report Count and logical bounds, and the
items that do leak past the Pop are physical units, which deskhop ignores entirely. A
descriptor that relied on Pop to restore a Report Size would parse at the wrong width.

**Button bitmaps are read as signed.** `get_report_value()` sign-extends its result
whenever the top bit of the field is set, which is right for X, Y, wheel and pan and
wrong for a button bitmap. An 8-button mouse with everything held reports `-1` rather
than `255`. `mx518_mouse` was the first device in the corpus with enough buttons to
reach bit 7, which is why this had not come up. Harmless as things stand -
`mouse_report_t.buttons` is `uint8_t`, so the low byte ships correctly either way - but
it is a signed read of a bitfield, and `state->mouse_buttons` holds the sign-extended
value as `int16_t` in the meantime.

`bolt_rx_iface1` sharpens it. That mouse declares a **16-bit** button field, so the
sign extension reaches much further: holding button 16 alone reads `-32768`, and all
sixteen together read `-1`. Both are in `make mouse`. The truncation to `uint8_t` is no
longer harmless either, because buttons 9 to 16 have nowhere to go at all - whatever
happens to the sign, they cannot reach the output PC through a one-byte field.

## What this harness cannot see

`compare` diffs the *parse*. It compiles `hid_parser.c` and `hid_report.c` and stubs
the four `process_*_report` receivers, so a change confined to `keyboard.c` is
invisible to it.

PR [#358] is exactly that change: it teaches `process_consumer_report` and
`process_system_report` not to skip a leading report ID byte that isn't there. The
parse is untouched, so `compare` still prints `identical parse` on all 47
descriptors including `cherry_kc6000_consumer`, the device it fixes. That is the
correct answer to the question `compare` asks, and the wrong answer to "does this PR
do anything".

**`make consumer` now answers that second question.** Both receivers are lifted by
`tools/lift.py` the way `get_keyboard` and the mouse extractors already were, and
what they hand to the send path is recorded and asserted. The cut is one level below
them: `send_consumer_control` and `send_system_control` are *not* lifted, because
they reach `queue_cc_packet`, `time_us_64()` and `state->last_activity[BOARD_ROLE]`.
Those three, plus `queue_packet` and `global_state`, are recorded stand-ins in
`src/recorders.c`, which is exactly the boundary where a test can see the decision a
receiver made.

#358 changes function bodies only - no macro, no header change - so `cases_kbd.h`'s
`#ifdef MAX_NKRO_BLOCKS` trick has nothing to test. Every case therefore carries both
answers and `cctest` **classifies** the branch rather than assuming it, ending in a
verdict line:

```
  separating rows: 7 behave like main, 0 like #358
  VERDICT: this branch does NOT have the #358 fix
```

A run where no separating row fires exits non-zero rather than reporting success, on
the same grounds as `fuzz`'s `touches == 0` guard: a classifier that classified
nothing has stopped testing what it exists for.

Keyboard reports used to be described here as the same kind of gap, on the grounds
that the NKRO bitmap is unpacked in `keyboard.c`. That was wrong. `extract_kbd_data`
and its three helpers live in `hid_report.c`, which every binary here already
compiles; only the declaration sits in `keyboard.h`, and `keyboard.c` just calls in.
`make kbd` covers that path now, which is how [#359] got checked at decode level and
how the [#216] suspicion about the Model 100's bit-68 bitmap got cleared.

### Everything above runs on the host

That is the real limit. The harness compiles the firmware's own parser and decode
path and feeds them real bytes, which is enough to say what the code does with a
report, but it never enumerates a device, never negotiates a protocol and never
sees TinyUSB's host stack. A finding here is a claim about the firmware, not about
a keyboard on a desk.

[`emu/`](emu/) closes part of that for the collection collapse. It builds an RP2040
into a stand-in for the 8BitDo Retro Mechanical Keyboard of [#57], presenting that
device's 245 bytes over real USB and typing a fixed line through two of its three
keyboard collections. Plugged into a deskhop board it prints `abcdef,./` on firmware
with the fix and `gmovw3,./` on firmware without it, and drops the `,./` tail
entirely if deskhop is in boot protocol, where the test could otherwise pass for the
wrong reason.

It matters because the collapse needs hardware this project does not have: the two
devices dumped here are a Logi Bolt receiver and a Keychron Ultra-Link 8K, and the
affected keyboards are the ones in [#57], [#211] and [#295]. Without the rig, a device
test depends on one of those reporters. The descriptor is generated out of
`descriptors.h` at build time and every report the rig sends is pinned in
`k_bitdo_cases`, with the broken column measured against the pre-fix tree and the
fixed column against the branch, so the hardware and the host cannot quietly come to
disagree about what the device is.

**Confirmed on hardware, 2026-08-19.** An RP2040 running the rig, plugged straight
into a PC, types `abcdef,./` - the OS parses the descriptor correctly, so the rig is
faithful. The same board plugged into a DeskHop Extended v1.04 keyboard port types
`gmovw3,./`, on every one of eleven lines. That is the collapse, on real silicon,
matching the number `k_bitdo_cases` predicted from the host.

**The fix, on the same hardware.** Flashed with DeskHop Extended v1.05 and left
running, the same board through the same port typed `abcdef,./` for 48 lines with
nothing else changed. So both halves are measured on silicon, not just on the host.

Two of the eleven DeskHop Extended v1.04 lines came through as `gmovw3,.`, losing the
final `/`. That was written up here as unrelated to the collapse, on the grounds that
`add_keys` deduplicates before transmission, so the doubled NKRO walk
`{54,55,56,54,55,56}` and the fixed `{54,55,56}` reach the host as the same three
keycodes and the tail is bit-identical either way. The prediction attached to it was
that the truncation would survive the fix. It did not: 48 clean lines, which at a two
in eleven rate is under a hundredth of a per cent. The dedup argument was answering
the wrong question, and the truncation belongs to the bug.

The likeliest remaining explanation is cost rather than content. On a collapsed
interface every incoming report, the 6KRO ones included, is walked as two 120-bit
bitmaps, and deskhop's host port is bit-banged on PIO where that much extra work
inside the report callback can cost a frame. That is a hypothesis with nothing
measuring it yet, recorded because the numbers rule out the explanation that was here
before, not because this one has been shown.

What it still does not cover: it is one emulated device on one interface, so it says
nothing about how a real 8BitDo negotiates, about the other two reported devices, or
about any bug that needs a hub, a composite device or a timing race to show up.

## Licence

GPL-3.0, matching deskhop. Not a formality: `tools/lift.py` copies function bodies
verbatim out of deskhop's GPLv3 sources and `tools/instrument.py` writes a modified
copy of `hid_parser.c`, so everything under `build/*/gen/` is a derivative work.
Both tools stamp that notice into what they generate, so a generated file read out
of the build directory still says where it came from.

<!-- upstream issues and PRs -->
[#57]: https://github.com/hrvach/deskhop/issues/57
[#117]: https://github.com/hrvach/deskhop/issues/117
[#211]: https://github.com/hrvach/deskhop/issues/211
[#295]: https://github.com/hrvach/deskhop/issues/295
[#133]: https://github.com/hrvach/deskhop/issues/133
[#215]: https://github.com/hrvach/deskhop/issues/215
[#229]: https://github.com/hrvach/deskhop/issues/229
[#216]: https://github.com/hrvach/deskhop/issues/216
[#297]: https://github.com/hrvach/deskhop/issues/297
[deskhop-extended]: https://github.com/mglushko/deskhop-extended
[#332]: https://github.com/hrvach/deskhop/issues/332
[#335]: https://github.com/hrvach/deskhop/issues/335
[#358]: https://github.com/hrvach/deskhop/pull/358
[#359]: https://github.com/hrvach/deskhop/pull/359
[#361]: https://github.com/hrvach/deskhop/pull/361
[hidintro]: https://docs.kernel.org/hid/hidintro.html
[tmk]: https://github.com/tmk/tmk_keyboard/wiki/USB:-HID-Report-Descriptor
[rpigist]: https://gist.github.com/probonopd/9646c69f876ff2b4b879aeb1c1cbc532

<!-- dumping tools -->
[winhiddump]: https://github.com/todbot/win-hid-dump
[whd2]: https://github.com/todbot/win-hid-dump/issues/2
[usbipd]: https://github.com/dorssel/usbipd-win
[hidapi]: https://github.com/libusb/hidapi/blob/master/windows/hidapi_descriptor_reconstruct.c
