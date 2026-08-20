# Hardware emulators

Spare RP2040s turned into stand-ins for devices in the corpus, so findings can be
confirmed on real hardware rather than only on the host. Two of them, one per UF2:

| build | device | issue | what it is for |
|---|---|---|---|
| `bitdo-emu.uf2` | 8BitDo Retro Mechanical Keyboard `2dc8:5201` | [#57] | three keyboard collections on one interface |
| `gameball-emu.uf2` | Gameball trackball `0782:001B` | [#332] | Report Count 16328 against a 128 entry array |

Every report descriptor is pulled out of `../descriptors.h` by `gen_desc.py` at build
time rather than checked in twice, so an emulator and the corpus cannot drift apart.

# 8BitDo, the keyboard collection collapse

## What it presents

The real device's interface 2, byte for byte: 245 bytes of report descriptor
declaring **three keyboard collections on one interface**, on report IDs 1 (6KRO),
12 and 10 (both 120-bit NKRO bitmaps), plus consumer and system collections.

`gen_desc.py` pulls those bytes out of `../descriptors.h` on every build, so the
emulator and the harness corpus cannot drift apart. The interface is declared
`bInterfaceSubClass = 0`, `bInterfaceProtocol = 0`, which is both what a
six-collection composite interface really looks like and what keeps deskhop on the
report ID path, where the bug lives. Declaring it a boot keyboard would risk the
host negotiating boot protocol, where reports carry no ID and the bug is
unreachable.

## What it types

Every six seconds, one line:

| burst | collection | fixed firmware | broken firmware |
|---|---|---|---|
| keycodes 04 05 06 07 08 09 | 6KRO, report ID 1 | `abcdef` | `gmovw3` |
| usages 54 55 56 | NKRO, report ID 12 | `,./` | `,./` |
| usage 40 | NKRO, report ID 12 | newline | newline |

so the line reads **`abcdef,./`** when the fix is in and **`gmovw3,./`** when it is
not.

Those are measured, not guessed. Every report the rig sends is pinned in
[`../src/cases_kbd.h`](../src/cases_kbd.h) as `k_bitdo_cases`, with the broken
answers verified against the pre-fix tree and the fixed ones against the branch.
On a broken tree all three collections collapse onto `keyboards[0]`, the NKRO
bitmap descriptors overwrite the 6KRO key array, and an incoming report ID 1 gets
walked as if it were a bitmap: byte `0x04` at payload offset 2 lands on bit 2 of
the bitmap's second byte, giving usage 8 + 2 = 10 = `g`, and so on up the report.

The `,./` tail is the positive control, and it also guards the one way this test
could pass for the wrong reason.

That way is boot protocol. `extract_kbd_data` returns `_extract_kbd_boot` before it
consults the descriptor at all, and the 8BitDo's 6KRO layout **is** the boot layout,
so a nine-byte report ID 1 decodes to `abcdef` in boot protocol whether the fix is
present or not. deskhop only gets there if it was built with
`ENFORCE_KEYBOARD_BOOT_PROTOCOL 1` and the rig is on board A, which is not the
default and not the images here, but a test that cannot tell the difference is not
worth running.

So the control uses usages 54, 55 and 56 rather than letters. Those live in bitmap
bytes 6 and 7, at wire offsets 8 and 9, past the eight bytes `_extract_kbd_boot`
copies off the front of a 17-byte report. In boot protocol they contribute no
keycodes, so the tail disappears and the line breaks go with it. That behaviour is
pinned too, as `k_bitdo_boot_cases`.

## Build

```
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/deskhop-extended/pico-sdk
cmake --build build
```

Produces `build/bitdo-emu.uf2` and `build/gameball-emu.uf2`. Both, every time.

## Run it

1. Hold **BOOTSEL** on the spare RP2040, plug it into your PC, release. It mounts
   as `RPI-RP2`.
2. Copy `build/bitdo-emu.uf2` onto it. It reboots as the emulated keyboard.
3. Unplug it from the PC and plug it into the **deskhop board's USB-A host port**,
   the one a keyboard normally goes in. If `enforce_ports` is on it has to be the
   keyboard port specifically.
4. On the PC that deskhop board outputs to, open a text editor and leave it
   focused.
5. Wait about ten seconds for the grace period, then read the lines as they
   arrive.

To recover the board for reflashing, hold BOOTSEL while plugging it in.

## Reading the result

| what you see | meaning |
|---|---|
| `abcdef,./` in a column | the collections each got their own `keyboard_t`, fix confirmed |
| `gmovw3,./` in a column | the collapse is present, reports decode against the wrong collection |
| `abcdef` run together, no tail | boot protocol, this run proves nothing either way |
| `,./` alone | the 6KRO collection produced nothing, a third state worth reporting |
| nothing | the rig is not reaching the PC, check the port, cable and active output |

Doing it as an A/B is what makes it conclusive: flash the pre-fix image, confirm
`gmovw3,./`, then flash the fixed one and confirm `abcdef,./` with nothing else
changed. Run that way on 2026-08-19 it gave 11 lines of `gmovw3,./` on
deskhop-extended v1.04 and 48 of `abcdef,./` on v1.05, against 7 lines of
`abcdef,./` typed straight into a PC as the control.

# Gameball, the usages[] overflow

[#332] is titled "Not functional with Gameball trackball" and asks, in the reporter's
words, whether it could even work. The interesting part is that the interface carrying
the bug is not the interface you would watch.

## What it presents

All three interfaces, in the order the real device does:

| # | interface | bytes | class | sends |
|---|---|---|---|---|
| 0 | trackball | 77 | boot mouse | a 400 px circle, then both scroll pads |
| 1 | gesture | 350 | vendor, subclass 0 | nothing at all |
| 2 | keyboard | 38 | boot keyboard | nothing |

Interface 1 is the whole test. Its first report declares Report Count `0x3FC8`, which
is 16328, against `usages[HID_MAX_USAGES]` where `HID_MAX_USAGES` is 128. In
`parser_state_t` the member immediately after that array is `p_usage`, a pointer the
parser then dereferences, so a tree without a bound runs off the end of the array and
straight into it. Parsing this descriptor on upstream main aborts under ASan on the
host; on an RP2040 it corrupts a live pointer instead. Nothing is ever sent on that
interface, because the damage happens at parse time and enumerating is enough.

Interface 0 is what you watch. `bInterfaceProtocol` is deliberately `MOUSE`: the
trackball descriptor declares no report ID, so an interface presenting as `NONE` would
make `report_carries_id()` false and drop `pick_receiver()` into the
`report_handler[report[0]]` branch, where `report[0]` is the button byte and the
pointer gets routed by which buttons are held. `MOUSE` takes the direct branch, which
is both correct and what a real trackball does.

Interface 2 is presented and never used. It declares eight modifier bits, 48 bits of
padding and no key array at all, so it could only ever report modifiers, which would
look like a stuck Shift. That is the real device's descriptor, not a simplification.

## The End Collection encoding

All three Gameball descriptors encode End Collection as `0xC1 0x00`, bSize 1 where
the spec says 0, at four places. deskhop skips the item by its declared size and is
unaffected. Windows is not so relaxed: the descriptor as dumped enumerates and is
then suspended with no driver bound, and `gameball-c0-emu`, which is the same
interface with only those items rewritten to `0xC0`, works. That is the difference
between LED code 2 and a moving pointer, and nothing else changed between them.

So use **`gameball-c0-emu.uf2`** on a PC. `gameball-min-emu.uf2` is the same
interface with the bytes exactly as dumped, kept so the comparison stays live, and
`gameball-emu.uf2` is the full three interface device.

The open question is recorded in `../descriptors.h`: #332's dump was taken on
Windows and shows HID collection paths, so Windows accepted whatever that device
really sent, which suggests the dump was re-encoded and the corpus bytes are a
transcription artifact rather than the device's own.

## Reading the result

The pointer circles continuously, 400 pixels across, once every 1.6 seconds.
After three revolutions it stops and works the scroll pads instead, up three, down
three, then pan right three and left three, and then goes back to circling. The LED
is solid while it is circling and flickers while it is scrolling, so you can tell
which phase it is in without watching the screen.

| what you see | meaning |
|---|---|
| circling, then scrolling, repeating | the descriptor parsed safely and the trackball works |
| circles, no scrolling | the pads are not reaching the host, worth reporting |
| LED solid but the pointer is still | reports are leaving the rig and nothing is acting on them |
| nothing, and the board stops responding | the parse took the firmware with it |

The circle is 64 relative steps whose deltas sum to zero on both axes, so the
pointer returns to where it started every revolution rather than walking across the
desktop, and each step is the difference between rounded points on the true circle,
so the path never leaves it by more than half a pixel.

Two things will still make it look off, and neither is the rig. Rounding each step
to a whole number makes the speed vary around the loop, and **pointer acceleration**,
which Windows calls "enhance pointer precision", turns speed variation into shape
distortion; no relative pointing device draws a true circle through it. And a circle
this size started near a **screen edge** gets clamped, which flattens that side and
loses the movement, so the loop stops closing and walks off across the desktop. Park
the pointer near the middle before plugging in, and turn acceleration off if you want
the shape to mean anything. deskhop also switches outputs on screen edges of its own
accord.

# Both rigs

## If nothing appears

The onboard LED reports which region the fault is in, so "nothing typed" does not
have to cover everything from an unpowered board to a working rig pointed at the
wrong PC.

| LED | meaning | where to look |
|---|---|---|
| dark | no power | cable, and whether the port supplies VBUS |
| one flash a second | powered, never enumerated | the host port, the cable, try it straight into a PC |
| two flashes a second | enumerated and armed, counting out the grace period | nothing, wait for it |
| rapid blinking | enumerated, endpoint never goes ready | the host stack, not the rig |
| **solid** (Gameball) | **sending motion, the pointer should be moving right now** | downstream of the rig, see below |
| flickering (Gameball) | working the two scroll pads | downstream of the rig |
| on, dipping three times a line (8BitDo) | typing | downstream of the rig |

The first four are shared. The last three mean the rig is doing its job and the
fault, if any, is past it. Solid with a motionless pointer is the informative one:
reports are leaving the board and not being acted on.

Armed and stalled both sit between enumeration and the first line, and they look
the same from outside, but one clears itself within ten seconds and the other never
does. They are separate patterns so a healthy rig waiting to start is not mistaken
for a stuck one.

If the LED is dipping, the emulator is doing its job and the fault is past it:

1. **Plug the rig straight into a PC**, bypassing deskhop. It should type
   `abcdef,./` on its own, because the OS parses the descriptor correctly. This
   splits the problem in half in about thirty seconds: typing here means the rig is
   sound and deskhop is the variable, nothing here means the rig is.
2. **Check which output is active.** `send_key` queues locally only when
   `CURRENT_BOARD_IS_ACTIVE_OUTPUT`, otherwise it sends the report to the other
   board over UART. Plugged into the non-active board with the other board not
   attached to a PC, every keystroke leaves and never lands.
3. **Check it is the USB-A host port**, not the port that goes to the PC.
4. **Check the window has focus.** The rig types into whatever is focused, and it
   does not care whether that is a text editor.

`enforce_ports` is not a candidate here. It is only tested in the keyboard and mouse
branches of `tuh_hid_mount_cb`, and this device enumerates as
`HID_ITF_PROTOCOL_NONE`, so it takes neither. For the same reason deskhop never sets
`keyboard_connected` for the rig, so the board's keyboard LED indicator stays off
even when everything is working. That is expected and does not affect key delivery.

## Notes

It types on a timer with no input of its own, because the RP2040 has no button
other than BOOTSEL and reading that while USB is live is more risk than the test
is worth. Unplug it when you are done.

It uses the real device's VID and PID, `2dc8:5201`, so deskhop sees what the
reporters plugged in. Nothing in deskhop keys off VID or PID, so change them in
`usb_descriptors.c` if a cloned ID upsets a host's device cache. The USB strings
say `deskhop-hidtests` and `8BitDo Retro emulator` so it is never mistaken for the
real hardware on a bus scan.

[57]: https://github.com/hrvach/deskhop/issues/57
[#57]: https://github.com/hrvach/deskhop/issues/57
[#211]: https://github.com/hrvach/deskhop/issues/211
[#295]: https://github.com/hrvach/deskhop/issues/295

[#332]: https://github.com/hrvach/deskhop/issues/332
