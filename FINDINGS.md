# Findings

What this harness has measured that is still a defect on upstream `main` or on DeskHop
Extended, with the numbers kept as the record of how each was found. Findings since
fixed on both, the pull requests measured along the way and the confirmations on
hardware are in [FINDINGS-ARCHIVE.md](FINDINGS-ARCHIVE.md). The [README](README.md) has
the tool and the reference numbers; [CORPUS.md](CORPUS.md) has the devices. Issue and PR
numbers are [hrvach/deskhop](https://github.com/hrvach/deskhop)'s unless marked otherwise.

## Open findings

Grouped by what each one costs. Every one is present on both trees but the last group's,
which DeskHop Extended has fixed and upstream `main` has not.

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

This is **not** the same finding as the short reports, and the two are easy to conflate
because both show up as truncation. Those are about the *report*, in `hid_report.c` and
`mouse.c`, and fixed on both trees but for the boot-protocol mouse below; this one is about
the *descriptor*, in the parse loop, and is still open everywhere - `make truncate` fails on
roughly half of all prefixes even on a tree carrying every fix measured here. The count is
in the README's reference table rather than repeated here, because repeating it is how
this sentence came to quote a corpus one device out of date.

Reproduce the smallest case with:

```sh
make all && ./build/<target>/truncate gameball_trackball 1
```

**Report Count is a 32-bit field.** Visible in `timing`: memory stays intact after the
fix, but a large enough count still outruns the 500 ms watchdog.

### Functionality lost on a real device

One device, one capability.

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
overwrites `mouse.report_id` with 2. `extract_value()` rejects any report whose leading
ID byte does not match:

```c
if (uses_id && *raw_report != src->report_id)
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

**A fifth key section is lost.** `MAX_NKRO_BLOCKS` is 4 and the Wooting declares exactly
4, so there is no headroom: a keyboard splitting its bitmap five ways loses the last
section, silently, the way `main` before [#359] lost all but one. Deciding `is_nkro` on the
summed width does not change that - the fifth block is never recorded, so its bits are not
in the sum either. Nor does the width arm from [#366], which changes which blocks qualify
and not how many fit.

### Fixed in DeskHop Extended, open upstream

**A boot-protocol mouse report is read past its end.** `extract_report_values()` returns
early when the protocol is BOOT and reads buttons, X, Y, wheel and pan through a five-byte
`hid_mouse_report_t *` without consulting `len`, from `mouse.c:313` on `7c1e7b2`. The boot
report is defined only as far as buttons, X and Y, and plenty of mice stop there or after
the wheel, so on a mouse that `force_mouse_boot_mode` puts in boot protocol, wheel and pan
or pan alone are read from past the end, out of TinyUSB's shared endpoint buffer.
`make shortreport` counts 16 of 3355 on upstream, lengths 1 to 4 of all four
`mouse/boot_mouse/boot` rows, and nothing else since `3b9ac8c` bounded the other three
causes; DeskHop Extended takes only what arrived since `fe908d0` and counts 0. The four
causes, and how each was closed, are in the [archive](FINDINGS-ARCHIVE.md#fixed).

```sh
./build/<target>/shortreport mouse/boot_mouse/boot 0 1
```

**Two pointing devices cancel each other's buttons.** A mouse report carries the complete
button state of its sender, so a trackball reporting movement with nothing pressed says
"no buttons" as loudly as a keyboard's mouse keys say "left down". Upstream takes the newest
report at its word, so holding a button on one device and moving with the other releases
it. That is upstream [#287], reported for exactly that pair of devices, and the fix is to
send the union across every device the way `combine_kbd_states` already does for keyboards.
DeskHop Extended does; [#365] offers the same upstream and is still open.

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
[#287]: https://github.com/hrvach/deskhop/issues/287
[#332]: https://github.com/hrvach/deskhop/issues/332
[#358]: https://github.com/hrvach/deskhop/pull/358
[#359]: https://github.com/hrvach/deskhop/pull/359
[#361]: https://github.com/hrvach/deskhop/pull/361
[#365]: https://github.com/hrvach/deskhop/pull/365
[#366]: https://github.com/hrvach/deskhop/pull/366

<!-- dumping tools -->
[winhiddump]: https://github.com/todbot/win-hid-dump
[hidapi]: https://github.com/libusb/hidapi/blob/master/windows/hidapi_descriptor_reconstruct.c
