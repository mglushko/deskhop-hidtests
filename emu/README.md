# Hardware emulators

Spare RP2040s turned into stand-ins for devices in the corpus, so findings can be
confirmed on real hardware rather than only on the host. One per UF2:

| build | device | issue | what it is for |
|---|---|---|---|
| `bitdo-emu.uf2` | 8BitDo Retro Mechanical Keyboard `2dc8:5201` | [#57] | three keyboard collections on one interface |
| `ultralink-emu.uf2` | Keychron Ultra-Link 8K `3434:d028` | [#324] | an NKRO usage range one wider than its block |
| `gameball-emu.uf2` | Gameball trackball `0782:001B` | [#332] | Report Count 16328 against a 128 entry array |
| `sculpt-emu.uf2` | Microsoft Sculpt receiver `045e:07a5` | [#367] | a mouse on report ID 26, above the handler table |

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

Produces every UF2 in the table above, every time.

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

# Keychron Ultra-Link, the off-by-one usage range

Same procedure, `ultralink-emu.uf2`, and the same three-burst line. Interface 1 as the
real dongle presents it: a 6KRO keyboard on report ID 7, consumer control on 0x0C, and an
NKRO keyboard on 0x11 whose bitmap declares 153 usages over 152 bits.

The 6KRO report here is seven bytes, modifier and six keycodes, because this collection
declares no reserved byte - its 5 + 3 LED bits are Output. The NKRO bursts are chosen so
their bits land past the eight bytes `_extract_kbd_boot` copies: a usage sits at report
byte `2 + usage / 8`, so anything at 48 or above is outside it.

| what you see | meaning |
|---|---|
| `abcdef,./` and a newline | the 0x11 bitmap is being decoded, fix confirmed |
| `abcdef` and nothing else | the bitmap was rejected, which is [#324] |
| `abcdef` with a stray key before the newline | the collections collapsed as well, see [#57] |
| nothing | the rig is not reaching the PC, check the port, cable and active output |

Every report this rig puts on the wire is pinned as a case row against
`d_ultralink_iface1` in `../src/cases_kbd.h`, so the script cannot quietly stop meaning
what this table says.

Doing it as an A/B is what makes it conclusive: flash the pre-fix image, confirm
`gmovw3,./`, then flash the fixed one and confirm `abcdef,./` with nothing else
changed. Run that way on 2026-08-19 it gave 11 lines of `gmovw3,./` on
DeskHop Extended v1.04 and 48 of `abcdef,./` on DeskHop Extended v1.05, against
7 lines of `abcdef,./` typed straight into a PC as the control.

# Gameball, the usages[] overflow

[#332] is titled "Not functional with Gameball trackball" and asks, in the reporter's
words, whether it could even work. The interesting part is that the interface carrying
the bug is not the interface you would watch.

## What it presents

All three interfaces, in the order the real device does:

| # | interface | bytes | class | sends |
|---|---|---|---|---|
| 0 | trackball | 77 | boot mouse | a 120 px circle, then both scroll pads |
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
the spec says 0 - every End Collection item in the three, five in total. deskhop
skips the item by its declared size and is unaffected. Windows is not so relaxed:
the descriptor as dumped enumerates and is then suspended with no driver bound, and
`gameball-c0-emu`, which is the same interface with only those items rewritten to
`0xC0`, works. That is the difference between LED code 2 and a moving pointer, and
nothing else changed between them.

There is a second anomaly in the same bytes, and it is the one that produces a
staircase rather than a circle. The trackball's X, Y, wheel and pan carry no
Logical Minimum of their own, so the 0 left over from the button block stands and
`25 7F` caps them at 127: all four read as **unsigned 0..127**. A host honouring
that discards every negative delta, so the pointer can only travel right and down,
wherever on screen it starts.

deskhop is unaffected by either. `get_report_value` takes the sign from the field
width rather than the declared range, which `make mouse` confirms at 22/22 with
`0xEC` decoding to -20. Both anomalies are evidence about the dump, not the parser,
and together they suggest #332's descriptor was reconstructed from parsed data
rather than captured: a reconstruction is what loses an inherited global item and
re-encodes End Collection with a size byte.

### Which build to flash

| build | interfaces | descriptor | for |
|---|---|---|---|
| `gameball-full-emu` | all three | both repairs | **testing #332** |
| `gameball-fixed-emu` | trackball | both repairs | a working pointer, nothing else |
| `gameball-c0-emu` | trackball | End Collection only | isolating the second anomaly |
| `gameball-min-emu` | trackball | exactly as dumped | isolating the first |
| `gameball-emu` | all three | exactly as dumped | the corpus bytes, unaltered |

The bottom two did not enumerate on the Windows PC used on 2026-08-19. They are
kept as separate builds so each finding keeps its own evidence.

`gameball-full-emu` is the one that tests the parser bug, because the Report Count
of 16328 lives on the gesture interface and only the three interface builds present
it. The repairs are applied there because a host has to accept the device before
anything can be tested at all, and the evidence says they move the bytes closer to
what the real device sends. The 16328 is untouched.

The open question is recorded in `../descriptors.h`: #332's dump was taken on
Windows and shows HID collection paths, so Windows accepted whatever that device
really sent, which suggests the dump was re-encoded and the corpus bytes are a
transcription artifact rather than the device's own.

## Measured on a PC, 2026-08-19

`gameball-fixed-emu` into a PC, read on the bench:

| what the rig sends | what the browser reported |
|---|---|
| wheel `+3` / `-3` | deltaY `-500` / `+500` |
| pan `+3` / `-3` | deltaX `+300` / `-300` |
| 48 step circle, radius 60 | radius 60 px, roundness 95% |

The signs are the part worth checking, and they are right in both directions. HID
wheel counts positive away from you while the browser counts deltaY positive
downward, so `+3` arriving as `-500` is correct. AC Pan and deltaX share a
direction, so `+3` arriving as `+300` is correct too. Both axes inverted, or
neither, would have meant something was wrong.

The two magnitudes differ because the host scales the axes separately, vertical by
its lines-per-notch setting and horizontal by characters-per-notch, not because
one axis is weaker than the other. And the measured radius matching the designed
radius says the host is not scaling the movement, so pointer acceleration was not
in play for that reading. The missing 5% of roundness has three plausible homes,
none of them faults: each step is rounded to a whole number, mousemove events are
coalesced so the sampled path is a subset of the real one, and any acceleration
at all would show up here.

## Through DeskHop Extended v1.05

Same rig, same bench, plugged into the keyboard port instead of the PC:

| | direct to PC | through deskhop |
|---|---|---|
| wheel `+3` / `-3` | deltaY `-500` / `+500` | same |
| pan `+3` / `-3` | deltaX `+300` / `-300` | same |
| circle, speed 13/22 | radius 60, roundness 95% | radius 60, roundness 92% |
| circle, speed 13/23 | | radius 61, roundness 94 to 95% |

**That is the answer #332 asks for.** The Gameball works: both scroll pads reach the
host with the right signs and magnitudes, and the pointer path survives the trip.

The three points of roundness are not acceleration. It was switched off for that
reading, and `calculate_mouse_acceleration_factor` returns 1.0 before touching its
curve when `enable_acceleration` is clear, so that path contributed nothing at all.

What is left is that deskhop scales the two axes differently. `update_mouse_position`
multiplies each axis by its own factor, and the defaults are

    MOUSE_SPEED_A_FACTOR_X 16
    MOUSE_SPEED_A_FACTOR_Y 28

which is a ratio of 1.75. They differ because deskhop sends absolute coordinates on a
0..32767 grid in both axes, and that grid covers a wider distance horizontally than
vertically on any landscape screen, so a raw delta would travel further sideways than it
should. 1.75 compensates for a screen of that aspect. A 16:9 screen is 1.778, so the
compensation is 1.6% short and a circle arrives 1.6% wide. On 16:10 it would be 9%
out, and on a 21:9 ultrawide about 33%.

The rule falls out of that: **`speed_y / speed_x` should equal the screen's aspect
ratio.** They are not two independent sensitivity knobs. Equal physical movement needs
`speed_x * width == speed_y * height`, so the ratio is `width / height` and nothing
else. Raising both together changes sensitivity; changing one alone stretches the
pointer.

The reading that produced 92% was taken with 13 and 22, a ratio of 1.692, on a
2560x1440 screen, which is exactly 16:9 and wants 1.778. That is 5.1% short, so the
path arrives 5.1% wide, which costs about 4.9 points of roundness on its own and lands
near 93% against the 5% baseline. Observed was 92%.

Changing `speed_y` alone from 22 to 23 gives 1.769, within 0.5% of the screen, and was
predicted to read near 95%. Measured: **94 to 95%, radius 61 px**. So the three points
were the aspect mismatch, and the rule holds.

Worth noting which way that cuts: the shipped default of 16 and 28 is 1.6% out on a
16:9 screen, while the customised 13 and 22 is 5.1% out. Lowering one speed on its
own makes the pointer anisotropic unless the other moves with it.

| screen | aspect | 13 / 22 is off by | speed_y wanted, keeping speed_x at 13 |
|---|---|---|---|
| 16:9 | 1.778 | +5.1% | 23 |
| 16:10 | 1.600 | -5.5% | 21 |
| 21:9 | 2.333 | +37.9% | 30 |
| 4:3 | 1.333 | -21.2% | 17 |

The bench measures this directly rather than leaving it to be inferred from a low
roundness figure, because "wobbly" and "stretched" are different faults with different
causes. **Aspect X:Y** comes from the second moments of the sampled path: for a shape
swept evenly, `mean(dx^2)` is `a^2/2`, so the semi-axes are `sqrt(2*mean(dx^2))` and
`sqrt(2*mean(dy^2))` and their ratio is the ellipse. 1.000 is round. Whatever it reads
is the factor `speed_y` is out by, so multiplying `speed_y` by it and re-running is the
correction.

## Reading the result

The pointer circles continuously, 120 pixels across, once every 1.2 seconds.
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

The circle is 48 relative steps whose deltas sum to zero on both axes, so the
pointer returns to where it started every revolution rather than walking across the
desktop, and each step is the difference between rounded points on the true circle,
so the path never leaves it by more than half a pixel.

Two things will still make it look off, and neither is the rig. Rounding each step
to a whole number makes the speed vary around the loop, and **pointer acceleration**,
which Windows calls "enhance pointer precision", turns speed variation into shape
distortion. And a circle started near a **screen edge**, or worse a corner where two
directions clamp at once, gets flattened on that side and loses the movement that
would have closed the loop, so it walks off across the desktop. 120 pixels is small
enough that this stops mattering much, but turn acceleration off if you want the
shape to mean anything. deskhop also switches outputs on screen edges of its own
accord.

# Microsoft Sculpt receiver, report ID 26

[#367] is a Sculpt Ergonomic Mouse that neither moves the pointer nor clicks. Its
receiver puts the mouse on report ID 0x1A, which is 26, and `main` binds receivers in a
table of `MAX_REPORTS` (24) slots indexed by the ID, so nothing is ever bound and every
report is dropped before decode. The parser has the layout right all along, which the
host harness shows; this rig shows the drop, and the fix, on a board.

## What it presents

All three interfaces, in the order the real receiver does, with the classes the public
probes of `045e:07a5` show:

| # | interface | bytes | class | sends |
|---|---|---|---|---|
| 0 | keyboard | 57 | boot keyboard | one F13 per cycle, the positive control |
| 1 | mouse | 223 | boot mouse | the circle, five clicks, wheel and tilt, on report 0x1A |
| 2 | consumer | 296 | vendor, subclass 0 | nothing |

Interface 1 is deliberately `MOUSE` rather than `NONE`. That is what the real unit
declares, and in report protocol the two take the same path through `usb.c` anyway,
since the descriptor uses report IDs. What `MOUSE` adds is reachability for
`force_mouse_boot_mode`: TinyUSB's host will not send `SET_PROTOCOL` to a `NONE`
interface, and the rig follows that request, switching to the three-byte boot layout
with no ID. What the real receiver appends in boot protocol was never captured, so the
rig sends the spec minimum and nothing for wheel or tilt.

## What it does

Every cycle: the same 120 pixel circle as the Gameball, three revolutions; then
buttons 1 to 5 pressed and released one at a time, button 5 being the one the
reporter's own patch masks off; then wheel up three, down three, tilt right three,
left three, the same deltas as the Gameball so the bench reads the same numbers for
both; then F13 on the keyboard interface. The LED is solid while it circles and
flickers through the rest.

Where the circle lands is deskhop's choice, not the rig's. deskhop sends absolute
coordinates and its position starts at the top-left corner at power-up; only a device
attached through deskhop moves it, and a mouse plugged straight into the PC does not. The
rig's deltas sum to zero, so it circles wherever deskhop's pointer was when it began, and
from a corner the first revolution clamps on two sides and leaves the circle tangent to
both edges, clicking and scrolling into whatever sits there. Before the run, put the
pointer over the bench's field with a mouse on the deskhop's other port, then leave that
mouse alone; the rig picks up from there on its next revolution, no replug needed.

The button and key readouts on the bench exist for this rig: keep the pointer over the
page and the five lamps light in turn, the held mask shows the HID button byte deskhop
put on the wire, and the key panel shows `F13`. Open the bench in a fresh tab with no
history, because Chrome navigates on buttons 4 and 5 whatever the page asks.

## Reading the result

| what you see | meaning |
|---|---|
| circle, five lamps, scroll on both axes, `F13` | report 0x1A is being delivered, fix confirmed |
| `F13` and nothing else, LED solid, pointer still | the mouse report is being dropped, the [#367] state |
| circle and lamps, no scroll | boot protocol, `force_mouse_boot_mode` is on: proves nothing about the fix |
| nothing, LED dipping | the rig is not reaching the PC, check the port, cable and active output |

The third row is why the scroll phase is skipped in boot protocol rather than sent
some other way. DeskHop Extended routes boot protocol by interface, so that path works
there with or without the fix, and a run that cannot tell the two apart should say so
by losing its scroll rather than by looking complete.

As an A/B: flash a stock image and confirm the second row, then the fixed one and
confirm the first, with nothing else changed. Every report this rig sends decodes
through the host harness as `m_sculpt_cases` in `../src/cases_mouse.h`, and the dispatch
rows for it in `../src/dispatchtest.c` go from dropped to `mouse` on the fixed tree.

# All rigs

## pointer-bench.html

A page for reading what a host actually receives, rather than guessing from whether
something moved. It sits next to this file, [pointer-bench.html](pointer-bench.html),
and is served from GitHub Pages at
<https://mglushko.github.io/deskhop-hidtests/emu/pointer-bench.html>. That is the same
file rather than a second copy, so edit it here and the hosted one follows.

It exists because "the pointer moved" and "the pointer moved correctly" are different
claims:

- **The trace is drawn over the scroll field**, in one box rather than two. The path
  is in screen coordinates and the field pans underneath it, so a circle holding still
  above a field sliding sideways is both signals read at once. It carries a fitted
  circle and a roundness percentage from the radius spread, taken between the 5th and
  95th percentile so one stray sample cannot decide the verdict. This is what says
  whether a circle is a circle, instead of squinting at the cursor.
- **Two channels read separately.** Horizontal is `deltaX`, which is where AC Pan
  arrives; vertical is `deltaY`, the wheel. Each shows the live value, an event count,
  a running sum and min/max, on a centre-zero meter so the sign is visible. If pan is
  not getting through, the horizontal channel stays at zero events while the vertical
  one counts up, which is a much clearer signal than a page that does not scroll.
- **The field** is 2400 pixels wide, ruled and labelled, and its grid travels with its
  cells rather than staying pinned to the viewport, so panning moves the whole thing by
  a readable amount instead of sliding labels over a stationary grid.
- **A wheel event log** carrying `deltaMode`, because pixel, line and page modes differ
  between hosts and change how the numbers should be read.
- **Buttons and keys.** One lamp per HID button, lit while held, with a press count, the
  held mask as the raw button byte, and the last key received. The page keeps every
  button to itself, so a right click does not open a menu over the readout and a middle
  click does not start autoscroll.

One thing it cannot separate: the browser sees coordinates after the OS pointer curve
has been applied, so with pointer acceleration on a low roundness figure is not
evidence against the rig. Turn acceleration off before reading that number.


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
| **solid** (Gameball, Sculpt) | **sending motion, the pointer should be moving right now** | downstream of the rig, see below |
| flickering (Gameball, Sculpt) | working the scroll axes, or on the Sculpt the buttons and the key | downstream of the rig |
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

It uses the real device's VID and PID, `2dc8:5201`, taken from the [#57] dump, so
deskhop sees the same identifiers as the reported device. Nothing in deskhop keys
off VID or PID, so change them in `usb_descriptors.c` if a cloned ID upsets a
host's device cache. The USB strings say `deskhop-hidtests` and `8BitDo Retro
emulator`, so a bus scan distinguishes it from the real hardware.

[57]: https://github.com/hrvach/deskhop/issues/57
[#57]: https://github.com/hrvach/deskhop/issues/57
[#211]: https://github.com/hrvach/deskhop/issues/211
[#295]: https://github.com/hrvach/deskhop/issues/295
[#324]: https://github.com/hrvach/deskhop/issues/324

[#332]: https://github.com/hrvach/deskhop/issues/332
[#367]: https://github.com/hrvach/deskhop/issues/367
