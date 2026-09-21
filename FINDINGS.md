# Findings

What this harness has measured that turned out to be a defect, open or fixed, with the
numbers and the hardware confirmations kept as the record of how each was found. The
[README](README.md) has the tool and the reference numbers; [CORPUS.md](CORPUS.md) has
the devices. Issue and PR numbers are [hrvach/deskhop](https://github.com/hrvach/deskhop)'s
unless marked otherwise.

## Before the bound: the `exhaust` crash

`exhaust` on upstream `main` before [#361], last measured at `59577cc`, was the one entry
here that was not reproducible. At 500 preceding usages `p_usage` had walked clean out of
`parser_state`, and this line wrote through it:

```c
*parser->p_usage = *(parser->p_usage - parser->usage_count);
```

Where that write landed was decided by the process memory map, so the same binary
segfaulted on some runs and printed a plausible-looking table on others - measured at
8 crashes in 10 with ASLR on, and 0 in 10 under `setarch -R`. Turning ASan and UBSan on
together is what made it reproducible: **that `main` failed 10 runs in 10**, with a
diagnosis rather than a signal. Which sanitiser caught it first still moved with the
layout - measured at 6 runs reporting ASan `global-buffer-overflow` in `store_element`
(`hid_parser.c:77`, the read of `*(parser->p_usage + i)`, which ASan calls "a wild
pointer") and 4 reporting a UBSan misaligned `uint16_t` store at `hid_parser.c:104` -
but a clean run was no longer one of the outcomes. Both are the same root cause: a
cursor that has left the object. [#361] bounds the cursor and `1e31d10` keeps the bound,
so from `54e3fe5` on `exhaust` is clean on upstream `main` 10 runs in 10, as it has been
on the fork. What both trees still show past 126 preceding usages is X and Y no longer
resolving, the open finding below.

The write itself was layout-dependent, so the 10 in 10 said the sanitisers caught it
reliably on that build, not that the underlying behaviour had become deterministic.

The corpus size fed into this too, though not by moving what sits next to
`parser_state`: the descriptor arrays are `static const` with initialisers, so
they land in `.rodata`, and what actually follows `parser_state` in the BSS is
`exhaust.c`'s own `iface` and `desc[16384]`, by link order. What changing the
corpus moved was the size of `.rodata`, and so where the BSS landed relative to
page boundaries and the heap - which was enough, because the write had already
left the object and its landing site was decided by the process memory map.
Removing one unrelated descriptor was once enough to change this from "usually prints
garbage" to "usually segfaults", and under the sanitisers it only changed which one
reported it.

## Upstream pull requests, measured

The parser PRs, measured the same way. All three have since closed, and each row ends
with how:

| PR | what `compare REF=main` shows |
|---|---|
| [#359] keep all key sections | every keyboard parses differently, as it must; `wooting_keyboard` gains all four blocks and `superlight2_rx_keyboard` all three. Nothing else in the corpus moves. `make kbd` carries this the rest of the way: on `main`, holding shift and `a` on the Wooting yields modifier `0x02` and no keycode, and on this branch the same bytes yield modifier `0x02` and keycode 4. Merged upstream on 2026-09-15 as `bff4d0c`; the readability pass `896e903` that followed renamed what it added, and the fork carries that spelling since `637b985`. Upstream `main` now yields the keycode too. |
| [#358] media keys without report IDs | identical parse on all 50 that parsed at the time, including `cherry_kc6000_consumer`, the device it fixes - which is the point, and why `make consumer` exists. `gameball_gesture` and `many_usages` crash on both sides, as they do on `main`. That target classifies it correctly: 7 separating rows, verdict "this branch has the #358 fix". Every report-ID device is unchanged. Merged upstream on 2026-09-11 as `6e10fa3` and simplified in `6124ef4`; the fork carries both, and upstream `main` now classifies as having the fix. |
| [#368] receivers looked up by report ID value | identical parse on 48 of the 50 that parse at all; `sculpt_rx_mouse` gains `26:M 31:C` and `apple_a2520_iface1` gains `82:C`, the two devices with a collection above ID 23. `make dispatch` carries it the rest of the way: the Sculpt's report 0x1A and the Apple's report 0x52 go from dropped to their receivers, 23 of the 33 rows dispatch then had with `main`'s routing and 33 of 33 with DeskHop Extended's. Compiled for the RP2040 it costs 22 bytes per interface, about 1 KB across `global_state`. Confirmed on the real receiver by #367's reporter. Closed on 2026-09-12 in favor of upstream's own fix, `ce8abb6`: a 256-entry map from report ID to receiver per interface, which binds any 8-bit ID with no guard at all, at 256 bytes per interface against this PR's 22. The fork carries that shape since `7dec931`. |

Three caveats on [#359], of which two are fixed and one stands.

**Fixed.** [#359] set `is_nkro` on the strength of any single block matching
`maps_usage_per_bit`, and `kbd_with_bit_field` is the case that found it. A plain
keyboard-page bit field matches that test - eight bits of F13
to F20 where the reserved byte usually sits is enough. That routes every report
through `_extract_kbd_nkro`, which never reads `key_array`, so an ordinary 6KRO
keyboard keeps its modifiers and loses every keycode. `main` before the merge was
unaffected because its `size > 32` filter ignored a block that small. Deciding `is_nkro`
on the summed width of all blocks keeps both: eight bits is padding, the Wooting's four
ranges are not.
Holding `a` on that device returns nothing before the fix and keycode 4 after, with
the rest of the corpus parsing identically.

**Still open.** `MAX_NKRO_BLOCKS` is 4 and the Wooting declares exactly 4, so there is no
headroom: a keyboard splitting its bitmap five ways still loses the last section, silently
and in the same way. Deciding `is_nkro` on the summed width does not change that - the
fifth block is never recorded, so its bits are not in the sum either. Nor does the range
rule below, which changes which blocks qualify and not how many fit.

**Also fixed**, and the sharper of the two, because it was a case where the new rule
rejected something `main` accepted. [#359] recognised an NKRO block by `maps_usage_per_bit`,
requiring `usage_max - usage_min + 1 == size`. The Keychron declares `19 00 2A 98 00` with
`95 98` - usage minimum 0, usage maximum 152, over 152 bits, which is 153 usages mapped onto
152 - so the block was never recorded and `nkro_count` stayed 0. `main` before the merge did
record it, on `size > 32`, and then threw it away at decode time when `_extract_kbd_nkro`
applied the identical 1:1 test. Both ended in `_extract_kbd_other` and lost every key, so it
was never a regression, but the stricter rule was silently dropping a real keyboard's bitmap
at parse time. DeskHop Extended, and upstream since it merged [#366] as `e5f8ae8`, ask
the two questions separately: the range must cover the block's bits, which is all
`extract_bit_variable` needs, and the item must then
look like a key bitmap - one usage per bit exactly, or a block at least `NKRO_MIN_BITS`
wide. The exact arm keeps the Wooting's 8-bit range and the Superlight2's 5- and 3-bit
ones, which a width rule alone would drop; the width arm takes the Keychron.
`ultralink_nkro_keyboard` decodes `11 00 10` to nothing before and to usage 4 after, and it
and `ultralink_iface1` were the only two of the 47 then in the corpus whose parse moved.
Upstream [#324](https://github.com/hrvach/deskhop/issues/324), sent as
[#366](https://github.com/hrvach/deskhop/pull/366) and merged as `e5f8ae8` with the
predicate renamed `is_nkro_key_field`, which the fork follows from `28dd847`. Until then
this was all that separated the two trees at parse time: `compare` differed on those two
and on `keychron_dongle_keyboard`, which joined the corpus later with the same shape, and
on nothing else in the 105; it now shows an identical parse on every entry.

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

### How #358 is measured

`compare` diffs the parse. It compiles `hid_parser.c` and `hid_report.c` and stubs the
four `process_*_report` receivers, and it lifts `get_keyboard` out of `keyboard.c`, so a
change anywhere else in that file is invisible to it.

PR [#358] is exactly that change: it teaches `process_consumer_report` and
`process_system_report` not to skip a leading report ID byte that isn't there. The
parse is untouched, so `compare` prints `identical parse` on every descriptor that parses
at all, including `cherry_kc6000_consumer`, the device it fixes. That is the
correct answer to the question `compare` asks, and the wrong answer to "does this PR
do anything".

**`make consumer` now answers that second question.** Both receivers are lifted by
`tools/lift.py` the way `get_keyboard` and the mouse extractors already were, and
what they hand to the send path is recorded and asserted. The cut is one level below
them: `send_consumer_control` and `send_system_control` are *not* lifted, because
they reach `queue_cc_packet`, `time_us_64()` and `state->last_activity[BOARD_ROLE]`.
Those two senders, plus `queue_packet` and `global_state`, are recorded stand-ins in
`src/recorders.c`, which is exactly the boundary where a test can see the decision a
receiver made.

#358 changes function bodies only - no macro, no header change - so `cases_kbd.h`'s
`#ifdef MAX_NKRO_BLOCKS` trick has nothing to test. Every case therefore carries both
answers and `cctest` **classifies** the branch rather than assuming it, ending in a
verdict line:

```
  separating rows: 7 behave like pre-#358 main, 0 like #358
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

## Open findings

Grouped by what each one costs. The last group is fixed and kept rather than
deleted, because those write-ups carry the measurements and, in two cases, the
confirmation on hardware. None of the open ones are addressed by the [#332] fix.

### Reads past a buffer, or misses a deadline

Nothing here corrupts memory on target, and both are bounded, but both are driven by
bytes a device chooses.

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

This is **not** the same finding as the short-report one below, and the two are easy to
conflate because both show up as truncation. That one is about the *report*, fixed in
`hid_report.c` and `mouse.c`; this one is about the *descriptor*, in the parse loop, and
is still open everywhere - `make truncate` fails on roughly half of all prefixes even on
a tree carrying every fix measured here. The count is in the README's reference table rather than
repeated here, because repeating it is how this sentence came to quote a corpus one
device out of date.

Reproduce the smallest case with:

```sh
make all && ./build/<target>/truncate gameball_trackball 1
```

**Report Count is a 32-bit field.** Visible in `timing`: memory stays intact after the
fix, but a large enough count still outruns the 500 ms watchdog.

### Functionality lost on a real device

One device, one capability. The Sculpt receiver that used to sit here is under fixed,
below.

**A collection nested inside another Application collection is lost.** The Cherry MW 8C's
interface 2, the third of its three, wraps its whole descriptor in one Application
collection and opens three
more inside it: a consumer array, a system control block and a vendor page.

`IS_BLOCK_END` means depth zero, and `handle_local_item()` promotes a `Usage` to
`global_usage` only there, so the `Usage (System Control)` naming the second block sits at
depth 1 and never becomes the global usage. It stays Consumer Control for the rest of the
descriptor. The system elements then reach `extract_data()` carrying `global_usage` 0x01
where the map row wants 0x80, nothing matches, and no ID, handler or receiver is recorded.
`make dump D=cherry_mw8c_consumer` finds the consumer block and no system block at all:
`handlers: 1:C`, `system: rid=0`. Sleep and wake up from that mouse can never arrive,
whatever the consumer fix does.

All three collections do share report ID 1, because the ID is declared once in the outer
collection before any of them opens, but that is incidental rather than causal - the block
would be dropped the same way if it declared an ID of its own.

**Fixing it exposes a second defect rather than finishing the job.**
`iface->report_handler[val->report_id] = hay->receiver` is unconditional and there is one
slot per ID, so once the system block matched it would overwrite the consumer's binding on
report 1: sleep and wake up would start working and the media keys would stop. One report ID
carrying two collections is something the routing cannot currently express, so the
recognition fix alone is a net loss on the only device that wants it.

The reach is that one device. Of the 47 descriptors the corpus had when this was measured,
18 have a collection whose naming `Usage` sits at depth 1 or deeper, and none of the other
17 opens an Application: 12 are
the ordinary `Usage (Pointer)` on a Physical collection inside a mouse, and the remaining
five nest a consumer collection, a vendor page or a digitizer `Usage (Finger)`. Leaving
`global_usage` alone is correct for every one of them, so promoting the usage at any depth
would break the other 17.

Not previously reported, and separate from [#358].

### Wrong data, with no device known to suffer it

Each is real in the code and each was found by measurement, but nothing captured in the
corpus is affected. Kept so the next person meets them here rather than in the field.

**The usage cursor never resets across a descriptor.** Visible in `exhaust` on any tree
since [#361]: a device
with more than about 126 usages ahead of its pointer collection enumerates without
the cursor moving. It is also visible on a shipping keyboard, which is the easier
case to argue from:

```sh
make dump D=ms600_consumer     # system: usage=0xFF02 page=0x0001
```

The Microsoft 600's system control collection declares a usage range (`19 00 29 FF`)
and no single usage of its own, and comes out carrying `0xFF02`, the last usage named
by the *vendor* block in the previous top-level collection.

**It reaches almost nothing, which is why it is still here.** Measured over the 47
descriptors the corpus had at the time and checked against `dump`: 42 elements in 23 of
them read a usage their block never declared. 30 land on a map row that wildcards the
usage, so the stale value is copied
into state and then read by nothing but `dump`; 11 match no row at all. Exactly one changes
decode - `d_composite`'s consumer padding bit holds `0x00B5`, Scan Next Track, on the
parsers before `1e31d10` and `0x0223`, AC Home, from it on, and `process_consumer_report`
has no `break`, so a set padding bit would *replace* a real key rather than add one. That descriptor is hand-written, from the section used to prove parser
changes are inert, so **no captured device here is affected**.

Two things for whoever does fix it. The carry it depends on was mislabelled until upstream
`1e31d10` of 11 September 2026: `hid_parser.c` said "Carry the last usage" and carried the
**first**, because after `p_usage += usage_count` the expression `*(p_usage - usage_count)`
was the old slot 0. `d_composite` showed it, declaring `00B5, 00B6, 00CD, 0223` and carrying
`00B5`. From `1e31d10` on, which DeskHop Extended carries, the carry is `*(p_usage - 1)` and
that padding bit holds `0223`, AC Home, so the same set bit now replaces a different real
key. The sentence above is right about the ms600 only because that block declared a single
usage.

And there is no settled answer to copy. HID 1.11 section 6.2.2.8 says local items do not
carry over to the next main item; Linux clears its whole local struct per main item and
then skips fields that declared no usage; FreeBSD clears the array but deliberately assigns
a saved `usage_last`; this parser does neither. Linux's version is not portable here, since
skipping depends on expanding `Usage Min..Max` into the usage array - 12288 slots there
against 128 here, and sixteen of the descriptors then in the corpus declared a range larger
than the whole array, `ms600_consumer` and `keyboardio_media` declaring 1024. What fits is
`return 0` when `usage_count` is zero - two lines in `get_usage()`, the same on upstream
`main` since `1e31d10` and on Extended - inert on every decode path, and on the current
shape it changes what `dump` prints for 39 of the 105 descriptors.

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
than `255`. `mx518_mouse` is where `make mouse` pins the value down, but it was not the
first here with enough buttons to
reach bit 7, which is why this had not come up. Harmless as things stand -
`mouse_report_t.buttons` is `uint8_t`, so the low byte ships correctly either way - but
it is a signed read of a bitfield, and `state->mouse_buttons` holds the sign-extended
value as `int16_t` in the meantime.

`bolt_rx_iface1` sharpens it. That mouse declares a **16-bit** button field, so the
sign extension reaches much further: holding button 16 alone reads `-32768`, and all
sixteen together read `-1`. Both are in `make mouse`. The truncation to `uint8_t` is no
longer harmless either, because buttons 9 to 16 have nowhere to go at all - whatever
happens to the sign, they cannot reach the output PC through a one-byte field.

### Fixed

Fixed in DeskHop Extended, and all but the short reports and the button union upstream as
well. Left in place because the reasoning and the numbers are the record of how each was
found and confirmed.

**Short reports read past the end of the buffer, in four separate places.** This
is the counterpart to the truncated-descriptor finding above, and the more serious
of the two: a descriptor arrives once at enumeration, a report arrives thousands of
times a second, and nothing checks either against the length the other implied. A
descriptor can declare a 30-byte NKRO bitmap and the device can then send eight
bytes; every offset the parser derived now points past the end.

`make shortreport` replays each `mouse` and `kbd` case at every length from its
receiver's floor up to full, in an exact-size allocation, forked, under ASan. On
`main` when this was written, 779 of 1191 failed; the README table carries the current
counts. The four distinct causes, each reproducible on its own:

```sh
make shortreport                          # the table
./build/<target>/shortreport mouse/mx518_mouse 0 5              # 1
./build/<target>/shortreport mouse/kensington_expert_mouse 0 1  # 2
./build/<target>/shortreport kbd/nkro_keyboard 0 8              # 3
./build/<target>/shortreport mouse/boot_mouse/boot 0 1          # 4
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
   Every report-ID device in the corpus with a mouse to extract fails at length 1 for this
   reason. `bolt_rx_touchpad` declares report IDs too, but nothing on it parses as a
   mouse, so no offset is ever read and it stays clean.

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

Those floors are hand copies of firmware logic, the last such copies here now that the
routing is lifted, and they are load bearing - raising a floor hides a finding and
lowering one invents a false one. They are commented as such in `src/shortreport.c`.

Present identically on `main` and on [#361] at the time: 779 of 1191 either way. The [#332]
work is in the parser, and all four of these are in the decode path.

Cause 3 is half closed. [#359], merged upstream on 2026-09-15, bounds `extract_bit_variable`
against the report length, and every keyboard row upstream decodes as a bitmap drops to
zero. What upstream still fails is causes 1, 2 and 4 on the mouse path and the
`_extract_kbd_other` loop, which runs to `MAX_KEYS` whatever length arrived: all seven
keyboard rows left with failures, the 8BitDo among them now that the bounded walk admits it
to the run, fault in that loop, at `hid_report.c:348` on `a0472e0`. DeskHop Extended guards
it with the report length as well, which is the whole distance between its 0 of 3355 and
upstream's 1372 of 3352 in the README table.

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

Four rows in `make dispatch` pass *by luck*, flagged as such. The target does not have
to be asked which routing it uses to know that - in boot protocol `report[0]` is data,
so routing must not depend on it, and the test simply perturbs that byte and sees
whether the receiver moves. Left Ctrl on a report-ID-1 keyboard is the common case.

**How `make dispatch` knows.** `tuh_hid_report_received_cb` is lifted whole. It reaches
`global_state` and two TinyUSB host calls, and `src/routing.c` supplies all three, so the
callback runs on the host exactly as written and the receivers it calls are stubs that
record which one was reached. Every tree is measured the same way, whichever shape its
routing takes. It was not always so: the callback was once modelled in `src/dispatch.h`,
with the real function lifted only on a tree that had factored the decision out, and a
model reports what it was written to say - which is exactly how this bug survived in the
first place. `mousetest` carried a copy of these rules, display-only and documented as
able to go stale, and it duly kept printing the old answer.

`usb.c` is byte for byte the same on `main` and on all three PRs, so this is upstream's
and long-standing rather than anything a fix introduced. [#372] carried the fix upstream and
merged on 2026-09-21 as `6f9e18c`: one local in the callback that says whether the wire
report carries an ID, which `4d113ac` behind it renamed `report_has_id` and DeskHop Extended
has in the same form. `make dispatch` on upstream `main` went from 26 of 36, 4 of those only
by luck and the 10 misrouted all boot protocol, to 36 of 36.
Both flags default to 0, so it is opt-in - but both are checkboxes on the
config page, and the mouse one has now been ticked on real hardware with the predicted
result. [#229] reports keys dying with that option enabled, which is *consistent* with
this - but that reporter's Wooting declares no report ID on its keyboard interface,
so treat the link as suggestive rather than established.

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
bits. Holding `a` produces keycode 10. On `main` before [#359] it was worse still: the
120-bit walk ran off the end of the 9-byte report, which is the unbounded
`extract_bit_variable` finding above showing up through this one. That is why `make kbd`
only carries this device on a tree that bounds the walk, and why `shortreport`, which
replays reports rather than descriptors, is where the overread itself is counted.

**Fixed in DeskHop Extended, and upstream since [#359] merged on 2026-09-15; the fix is its
fourth commit, `fff129f`.**
`get_keyboard()` is now lookup only and a parse-time `get_or_add_keyboard()` claims a slot
per report ID, so an interface holds one `keyboard_t` per collection:

```sh
make dump D=ultralink_iface1 DESKHOP=~/deskhop-extended   # keyboards: 2, keys=01111110
make dump D=bitdo_retro_iface2 DESKHOP=~/deskhop-extended # keyboards: 3, kbd[0] nkro=0
```

`kbd[0]` then matches `ultralink_keyboard`, the same collection parsed on its own, which
is the comparison those two entries were added for. `make kbd` went to 55 of 55 over the
15 devices it then had, and the three upstream reports that share this shape - [#57],
[#211] and [#295], one still open, one closed and reported as returning, one closed by the
reporter hard-coding a workaround for their own keyboard - are all this fault.

**Two pointing devices cancel each other's buttons.** A mouse report carries the complete
button state of its sender, so a trackball reporting movement with nothing pressed says
"no buttons" as loudly as a keyboard's mouse keys say "left down". Upstream took the newest
report at its word, so holding a button on one device and moving with the other released
it. That is upstream [#287], reported for exactly that pair of devices, and the fix is to
send the union across every device the way `combine_kbd_states` already does for keyboards.

Most of that is state handling above the decode path and out of this harness's reach.
One piece is not. A device that declares its buttons under a report ID of its own sends
movement reports carrying no button field at all, and `extract_report_values()` has always
had a fallback for that. Where the fallback looks now matters: reading the union would
write another device's buttons into this one's stored state, where they would stay held
after that device let go. It reads `iface->mouse_buttons` instead, and `run_button_fallback()`
in `mousetest.c` puts a deliberately different value in each so a fallback reaching for the
wrong one cannot pass.

`kensington_expert_mouse` is the device the cases are built on, buttons on report 1 and X/Y
on report 2. `gameball_trackball` and `ultralink_mouse` carry buttons in every report and
are there for the opposite claim, that the wire still wins where there is something on it:

```sh
make mouse DESKHOP=~/deskhop-extended   # 4 of 4 fell back to the interface that sent the report
```

**A report ID of 24 or above was never dispatched.** Fixed upstream in `ce8abb6` on
2026-09-12 and on the fork in `7dec931` the same day. The Microsoft Sculpt receiver ([#367])
puts its mouse on report ID 0x1A, which is 26. Upstream bound receivers in
`report_handler[MAX_REPORTS]`, indexed by the ID, and both the binding in `extract_data()`
and the lookup in `usb.c` were guarded by `report_id < MAX_REPORTS`, so nothing was ever
bound and every report was dropped before decode. The parser was not at fault: `make dump
D=sculpt_rx_mouse` derived every field at the right offset and `make mouse` decoded all
twelve reports the reporter captured, on every tree alike, none of which touched the table.
`make dispatch` showed both `sculpt rx mouse on ID 0x1A` rows dropped everywhere. Its
boot-protocol rows went on differing between trees until [#372] merged, because DeskHop
Extended routed boot protocol by interface while upstream read the button byte as an ID.
The Apple keyboard in [#157] has
the same shape, media keys on report 0x52, and lost them the same way. DeskHop Extended first
keyed the table by value, as `report_offsets` is, and sent that as [#368]; upstream chose a
256-entry map per interface instead, measured in the table above, and the fork now carries
that shape. `make dump` binds `26:M 31:C` and `82:C` on both trees, and `sculpt-emu.uf2`
from `emu/` puts the receiver on a desk for an A/B against a board.

**The rewritten carry read the slot behind the array.** Reported on `1e31d10` and fixed
upstream in `d7a453e` on 2026-09-12 with the guard described at the end; the fork carries it
as `9913a64`. On `1e31d10` the carry ran whether or not the block declared a usage, spelled
`*(p_usage - 1)`. On the first Input of a descriptor that declared none, which is buttons and
modifiers by `Usage Min..Max`, `p_usage` still pointed at slot 0, so the read was
`usages[-1]`. That is the high half of `usage_count`, the member before the array on the
RP2040 and on the host alike, and it is zero at that moment, so what landed in slot 0 was a
zero: formally out of bounds, harmless in practice, and one struct reordering away from not
being. 79 of the 105 descriptors here executed it. `make fuzz` reported it as `lowest index
touched: -1` under `highest index touched: 127`, 59101 accesses in 26717 of 40000
descriptors at seed 1, and exited non-zero for it; a highest in the thousands is the other
shape, the pre-fix cursor walking off the end. The guard skips the carry when `usage_count`
is zero, and with it both trees report a lowest index of 0 and nothing out of bounds.

## Confirmed on hardware

Everything above was measured on the host, which never enumerates a device, never
negotiates a protocol and never sees TinyUSB's host stack.

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

## Reconstructed captures

Avoid judging a descriptor from [win-hid-dump][winhiddump] output. It reconstructs from
Windows' parsed caps rather than reading the device, and the table under [Dumped here](CORPUS.md#dumped-here) lists five ways that went wrong on the
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

<!-- upstream issues and PRs -->
[#57]: https://github.com/hrvach/deskhop/issues/57
[#211]: https://github.com/hrvach/deskhop/issues/211
[#295]: https://github.com/hrvach/deskhop/issues/295
[#229]: https://github.com/hrvach/deskhop/issues/229
[#216]: https://github.com/hrvach/deskhop/issues/216
[#287]: https://github.com/hrvach/deskhop/issues/287
[#157]: https://github.com/hrvach/deskhop/issues/157
[#332]: https://github.com/hrvach/deskhop/issues/332
[#358]: https://github.com/hrvach/deskhop/pull/358
[#359]: https://github.com/hrvach/deskhop/pull/359
[#361]: https://github.com/hrvach/deskhop/pull/361
[#367]: https://github.com/hrvach/deskhop/issues/367
[#368]: https://github.com/hrvach/deskhop/pull/368
[#372]: https://github.com/hrvach/deskhop/pull/372

<!-- dumping tools -->
[winhiddump]: https://github.com/todbot/win-hid-dump
[hidapi]: https://github.com/libusb/hidapi/blob/master/windows/hidapi_descriptor_reconstruct.c
