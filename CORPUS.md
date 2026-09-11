# The corpus

`descriptors.h` holds 105 HID report descriptors: 93 captured from real devices and 12
synthetic probes. This page says where each real one came from, which tool read it, and
what it has caught so far. The [README](README.md) has the numbers and the findings; the
comments in `descriptors.h` have the bytes and the per-device notes.

Of the 93 real captures, 76 come from 31 upstream issues, 4 from dumps published
elsewhere, and 13 were dumped here from two devices on hand. The sections follow that
order.

## From upstream issues

The first 25 were collected while chasing individual reports. What they have bought so far:

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
  keycode 10 against the first three commits of [#359], which bound the bitmap walk without
  separating the collections; on `main` the unbounded walk keeps the device out of
  everything but its boot-protocol row. [#359] now carries the separation as a fourth
  commit, so `a` comes out as `a` against its head. It is the sharper version of the
  Keychron finding in the README's [open findings](README.md#open-findings).
- **Microsoft Wired Keyboard 600** ([#297]) is the cleanest reproduction of the stale
  usage cursor: its system control block comes out as `usage=0xFF02 page=0x0001`, an
  identifier it never declares, carried over from the vendor block in the preceding
  top-level collection.
- **Microsoft Sculpt Ergonomic Mouse receiver** (`045e:07a5`, [#367], "unable to move
  cursor, keys not working"), all three interfaces from the reporter's `usbhid-dump`. Its
  mouse sits on report ID 0x1A, which is 26, and `main` binds receivers in a table of
  `MAX_REPORTS` (24) slots indexed by the ID, so nothing is ever bound and every report is
  dropped before decode. `make dump D=sculpt_rx_mouse` shows the parse is right and the
  handlers line empty, `make mouse` decodes all twelve reports the reporter captured, and
  `make dispatch` shows them reaching nobody. The [open findings](README.md#open-findings) have the rest.
- **Apple Magic Keyboard with Touch ID** (`05ac:029f`, [#157], "will not work", and a board
  that reboots over and over). Three interfaces. The keyboard and the device-management
  interface come from the reporter's `usbhid-dump`; the third, Touch ID, sits on a bulk
  endpoint that Linux's HID driver never binds, so that tool never showed it and the
  emulation built from the dump could not reproduce the loop. Its 49 bytes were transcribed
  from the `lsusb -v` decode in the same thread: three vendor reports, one with a Report
  Count of 649. That is [#332] again. `make dump D=apple_a2520_touchid` aborts under ASan on
  `main` and on every open PR except [#361], where it parses and binds nothing. The
  keystrokes in the stream capture decode everywhere, and the media keys on report 0x52,
  which is 82, need [#368] the same way the Sculpt does.

## Published elsewhere

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
  the [open findings](README.md#open-findings): the second wins and the first goes dark.
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

## Dumped here

The last 13 came from two devices on hand, dumped with `usbhid-dump` after handing each
one to WSL with `usbipd-win`. They are the first entries here that cover a whole USB
interface as the firmware receives it, rather than one collection at a time:

- **Logi Bolt receiver** (`046d:c548`) contributes four interfaces. Interface 1 is the
  richest descriptor in the corpus: mouse, consumer control, system control and a fourth
  collection, on report IDs 2, 3, 4 and 0x0B. Its mouse declares **16 buttons**, as wide
  as anything here - `superlight2_mouse` matches it - and enough to reach bit 15 of a
  signed read, see the button finding among the [open findings](README.md#open-findings). Interface 3 is a Precision Touchpad, the
  largest descriptor here at 429 bytes, and the only one using Push and Pop; it parses to
  nothing at all, which is the right answer and is now asserted rather than assumed.
- **Keychron Ultra-Link 8K** (`3434:d028`) contributes five. Interface 1 carries a 6KRO
  keyboard on report ID 7, consumer control on 0x0C, and an NKRO keyboard on 0x11 - and
  it is the entry behind two of the README's [open findings](README.md#open-findings). One of them, the collection collapse,
  is invisible unless the whole interface is parsed at once, which is exactly why the
  interface-level entries exist; the other, the off-by-one usage range on the 0x11 bitmap,
  is why that collection is also here on its own.

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
matter; see [Adding a device](README.md#adding-a-device).

## From the September 2026 sweep

The last 51 came from a sweep of every upstream issue and comment in September 2026.
Each dump was run through `add_descriptor.py` and compared byte for byte, then item
for item, with what was already here. Only real-wire captures went in; the Windows
reconstructions the same sweep turned up were left out, since the table under [Dumped
here](#dumped-here) shows what those cost. Every one that declares a mouse or keyboard collection now has decode cases in
`src/cases_mouse.h` and `src/cases_kbd.h`; `dump`, `compare` and `truncate` take the rest. What their first run bought:

- **Apple Magic Trackpad** (`05ac:0265`, [#207]) is a fourth device with the [#332]
  shape: its mouse interface declares a 1387-byte input on report 0x44 against a single
  usage, and `make dump D=magic_trackpad_mouse` aborts under ASan on `main` in
  `store_element`, where the Gameball did. DeskHop Extended parses it as a three-button
  mouse on report ID 2.
- **Corsair Scimitar RGB Elite** (`1b1c:1b8b`, [#45]) is two descriptors for one device.
  The PC sees 172 bytes, with an NKRO keyboard collection on report ID 0x10 for the side
  panel; the DeskHop's TinyUSB, asking for those 172, was handed a 58-byte boot mouse
  instead. `scimitar_iface0` and `scimitar_boot_mouse` keep both.
- **Logi Bolt receiver at bcdDevice 5.01** ([#47]) declares its keys as three bitmap
  ranges where the corpus's own, newer Bolt declares a 6-slot array. `main` keeps only
  the 0x04-0x73 range and drops the IME keys, the [#215] finding on a second receiver
  firmware. The **Keychron 2.4 GHz dongle** ([#211]) reproduces the [#57] collapse on
  `main`: its 6KRO and NKRO collections land on one entry with `is_nkro` set.
- **Areson trackball** (`25a7:fa11`, [#23]) puts a keyboard collection with no report ID
  beside six collections that carry one, on a single interface. Both trees record it as
  report ID 0 on an interface they mark as using report IDs. What the device actually
  sends on that endpoint is not in the issue.

[#23]: https://github.com/hrvach/deskhop/issues/23
[#45]: https://github.com/hrvach/deskhop/issues/45
[#47]: https://github.com/hrvach/deskhop/issues/47
[#57]: https://github.com/hrvach/deskhop/issues/57
[#117]: https://github.com/hrvach/deskhop/issues/117
[#133]: https://github.com/hrvach/deskhop/issues/133
[#157]: https://github.com/hrvach/deskhop/issues/157
[#207]: https://github.com/hrvach/deskhop/issues/207
[#211]: https://github.com/hrvach/deskhop/issues/211
[#215]: https://github.com/hrvach/deskhop/issues/215
[#297]: https://github.com/hrvach/deskhop/issues/297
[#332]: https://github.com/hrvach/deskhop/issues/332
[#335]: https://github.com/hrvach/deskhop/issues/335
[#358]: https://github.com/hrvach/deskhop/pull/358
[#359]: https://github.com/hrvach/deskhop/pull/359
[#361]: https://github.com/hrvach/deskhop/pull/361
[#367]: https://github.com/hrvach/deskhop/issues/367
[#368]: https://github.com/hrvach/deskhop/pull/368
[hidintro]: https://docs.kernel.org/hid/hidintro.html
[rpigist]: https://gist.github.com/probonopd/9646c69f876ff2b4b879aeb1c1cbc532
[tmk]: https://github.com/tmk/tmk_keyboard/wiki/USB:-HID-Report-Descriptor
[whd2]: https://github.com/todbot/win-hid-dump/issues/2
[winhiddump]: https://github.com/todbot/win-hid-dump
