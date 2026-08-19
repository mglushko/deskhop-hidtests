# 8BitDo keyboard emulator

An RP2040 that pretends to be an [8BitDo Retro Mechanical Keyboard][57] and types a
fixed line, so the keyboard collection collapse can be confirmed on real hardware
rather than only on the host harness.

The point is that nobody working on this owns an affected keyboard. Three upstream
reports describe the bug ([#57], [#211], [#295]) and all three reporters have moved
on. This board stands in for them.

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

Produces `build/bitdo-emu.uf2`.

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

Doing it as an A/B is what makes it conclusive. Flash the deskhop board with the
pre-fix image, confirm `gmovw3xyz`, then flash the fixed image and confirm
`abcdefxyz` with nothing else changed.

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
