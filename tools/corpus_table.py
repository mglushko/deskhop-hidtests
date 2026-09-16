#!/usr/bin/env python3
"""Regenerate the table of every corpus entry in CORPUS.md.

    make corpus            # rewrites the block between the corpus-table markers

Size, top-level collections, report IDs and decode-case coverage are read from
descriptors.h and the three case tables, so they cannot drift. Device, IDs, interface,
source and tool are hand-kept in META below, because the comments in descriptors.h
say "the same receiver's interface 1" and a table cannot. An entry without a row here,
or a row without an entry, stops the run: the point of the table is that it is
complete, and a silent gap would be worse than no table.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import add_descriptor as ad  # noqa: E402
import hiditems  # noqa: E402

ROOT = os.path.join(HERE, os.pardir)
CORPUS_MD = os.path.join(ROOT, "CORPUS.md")
BEGIN, END = "<!-- corpus-table -->", "<!-- /corpus-table -->"

I = "https://github.com/hrvach/deskhop/issues/%d"


def issue(*nums):
    return ", ".join("[#%d](%s)" % (n, I % n) for n in nums)


HAND = "on hand"
SYN = "synthetic"
USBHID = "usbhid-dump"
MAC = "mac-hid-dump"
WIN = "win-hid-dump, reconstructed"
NG = "not given"

# entry: (device, VID:PID, interface, source, read with)
META = {
    # Gameball, issue 332
    "gameball_trackball": ("Gameball trackball", "0782:001b", "mi_00", issue(332), WIN),
    "gameball_gesture": ("Gameball trackball", "0782:001b", "mi_01 col02", issue(332), WIN),
    "gameball_keyboard": ("Gameball trackball", "0782:001b", "mi_01 col01", issue(332), WIN),
    # ordinary shapes and probes
    "boot_mouse": ("3-button boot mouse", "", "", SYN, "hand-written"),
    "hires_mouse": ("5-button mouse, 16-bit axes, report ID", "", "", SYN, "hand-written"),
    "boot_keyboard": ("boot keyboard", "", "", SYN, "hand-written"),
    "nkro_keyboard": ("NKRO keyboard, 240-bit bitmap", "", "", SYN, "hand-written"),
    "consumer": ("consumer control with a report ID", "", "", SYN, "hand-written"),
    "system": ("system control", "", "", SYN, "hand-written"),
    "sleepwake_emu": ("BOOTSEL Sleep/Wake rig, 8-bit system array", "", "", SYN, "hand-written"),
    "composite": ("keyboard, mouse and consumer on three report IDs", "", "", SYN, "hand-written"),
    "many_usages": ("Report Count 512 against one usage", "", "", SYN, "hand-written"),
    "system_no_report_id": ("system control with no report ID", "", "", SYN, "hand-written"),
    "vendor_then_mouse": ("vendor block ahead of the mouse axes", "", "", SYN, "hand-written"),
    "kbd_with_bit_field": ("6KRO keyboard with an F13-F20 bit field", "", "", SYN, "hand-written"),
    # from upstream issues, first pass
    "wooting_keyboard": ("Wooting Two HE", "31e3:1232", "mi_01", issue(335), WIN),
    "cherry_kc6000_consumer": ("Cherry KC6000 Slim", "046a:0113", "1", issue(117), USBHID),
    "ms600_keyboard": ("Microsoft Wired Keyboard 600", "045e:0750", "0", issue(297), USBHID),
    "ms600_consumer": ("Microsoft Wired Keyboard 600", "045e:0750", "1", issue(297), USBHID),
    "kensington_expert_mouse": ("Kensington Expert Mouse", NG, "0", issue(218), USBHID),
    "kensington_vendor": ("Kensington Expert Mouse", NG, "1", issue(218), USBHID),
    "keyboardio_keyboard": ("Keyboardio Model 100", NG, "2", issue(216), USBHID),
    "keyboardio_media": ("Keyboardio Model 100", NG, "3", issue(216), USBHID),
    "cherry_mw8_mouse": ("Cherry MW 8 Advanced", "046a:0114", "1", issue(133), USBHID),
    "cherry_mw8c_mouse": ("Cherry MW 8C Advanced", "046a:c117", "1", issue(133), USBHID),
    "cherry_mw8c_consumer": ("Cherry MW 8C Advanced", "046a:c117", "2", issue(133), USBHID),
    "superlight2_mouse": ("Logitech G Pro Superlight 2, wired", NG, "0", issue(215), USBHID),
    "superlight2_rx_keyboard": ("Logitech G Pro Superlight 2 receiver", NG, "1", issue(215), USBHID),
    "deskhop_keyboard": ("DeskHop Switch itself", "2e8a:107c", "mi_00 col01", issue(238), WIN),
    "deskhop_mouse": ("DeskHop Switch itself", "2e8a:107c", "mi_00 col02", issue(238), WIN),
    "bitdo_retro_iface2": ("8BitDo Retro Mechanical Keyboard", "2dc8:5201", "2", issue(57, 295), USBHID),
    "sculpt_rx_keyboard": ("Microsoft Sculpt Ergonomic Mouse receiver", "045e:07a5", "0", issue(367), USBHID),
    "sculpt_rx_mouse": ("Microsoft Sculpt Ergonomic Mouse receiver", "045e:07a5", "1", issue(367), USBHID),
    "sculpt_rx_consumer": ("Microsoft Sculpt Ergonomic Mouse receiver", "045e:07a5", "2", issue(367), USBHID),
    "apple_a2520_iface0": ("Apple Magic Keyboard with Touch ID, A2520", "05ac:029f", "0", issue(157), USBHID),
    "apple_a2520_iface1": ("Apple Magic Keyboard with Touch ID, A2520", "05ac:029f", "1", issue(157), USBHID),
    "apple_a2520_touchid": ("Apple Magic Keyboard with Touch ID, A2520", "05ac:029f", "2", issue(157), "lsusb -v decode, transcribed"),
    # published elsewhere
    "mx518_mouse": ("Logitech MX518", "046d:c08e", "0", "[tmk_keyboard wiki][tmk]", USBHID),
    "kernel_multi_collection": ("multi-collection composite", "", "", "[kernel HID docs][hidintro]", "published listing"),
    "rpi_keyboard": ("Raspberry Pi wired keyboard", "04d9:0006", "0", "[a gist][rpigist]", "published dump"),
    "pixart_mouse": ("PixArt/HP optical mouse", "093a:2510", "0", "[kernel HID docs][hidintro]", "published listing"),
    # dumped here
    "bolt_rx_keyboard": ("Logi Bolt receiver", "046d:c548", "0", HAND, USBHID),
    "bolt_rx_iface1": ("Logi Bolt receiver", "046d:c548", "1", HAND, USBHID),
    "bolt_rx_consumer": ("Logi Bolt receiver", "046d:c548", "1, consumer collection", HAND, USBHID),
    "bolt_rx_system": ("Logi Bolt receiver", "046d:c548", "1, system collection", HAND, USBHID),
    "bolt_rx_hidpp": ("Logi Bolt receiver", "046d:c548", "2", HAND, USBHID),
    "bolt_rx_touchpad": ("Logi Bolt receiver", "046d:c548", "3", HAND, USBHID),
    "ultralink_mouse": ("Keychron Ultra-Link 8K", "3434:d028", "0", HAND, USBHID),
    "ultralink_iface1": ("Keychron Ultra-Link 8K", "3434:d028", "1", HAND, USBHID),
    "ultralink_keyboard": ("Keychron Ultra-Link 8K", "3434:d028", "1, keyboard on report 7", HAND, USBHID),
    "ultralink_nkro_keyboard": ("Keychron Ultra-Link 8K", "3434:d028", "1, NKRO on report 0x11", HAND, USBHID),
    "ultralink_qmk_raw": ("Keychron Ultra-Link 8K", "3434:d028", "2", HAND, USBHID),
    "ultralink_vendor_8c": ("Keychron Ultra-Link 8K", "3434:d028", "3", HAND, USBHID),
    "ultralink_vendor_c1": ("Keychron Ultra-Link 8K", "3434:d028", "4", HAND, USBHID),
    # the September 2026 sweep
    "g502_mouse": ("Logitech G502, wired", "046d:c332", "mouse", issue(17), MAC),
    "g502_iface1": ("Logitech G502, wired", "046d:c332", "keyboard, consumer, HID++", issue(17), MAC),
    "unifying_rx_keyboard": ("Logitech Unifying receiver", "046d:c52b", "0", issue(17), USBHID),
    "unifying_rx_iface1": ("Logitech Unifying receiver", "046d:c52b", "1", issue(17), USBHID),
    "unifying_rx_hidpp": ("Logitech Unifying receiver", "046d:c52b", "2", issue(17), USBHID),
    "qmk_keyboard": ("QMK keyboard, Keychron Q6 and three others", "3434:0160", "0", issue(17, 30, 61, 151), USBHID),
    "qmk_shared_endpoint": ("QMK shared endpoint, Keychron Q6 and two others", "3434:0160", "2", issue(17, 30, 61), USBHID),
    "areson_trackball": ("Areson trackball, ProtoArc or Kensington Orbit", "25a7:fa11", "0", issue(23), "hexdump of sysfs"),
    "ps2_converter_keyboard": ("PS/2-to-USB converter", NG, "0", issue(26), USBHID),
    "ps2_converter_iface1": ("PS/2-to-USB converter", NG, "1", issue(26), USBHID),
    "issue26_mouse": ("USB mouse, unnamed", NG, "0", issue(26), USBHID),
    "scimitar_iface0": ("Corsair Scimitar RGB Elite", "1b1c:1b8b", "0", issue(45), USBHID),
    "scimitar_boot_mouse": ("Corsair Scimitar RGB Elite, as handed to DeskHop", "1b1c:1b8b", "0", issue(45), "TinyUSB debug log"),
    "corsair_ffc2_iface1": ("Corsair Scimitar and Strafe", "1b1c:1b8b, 1b1c:1b20", "1", issue(45), USBHID),
    "strafe_iface0": ("Corsair Strafe RGB, normal mode", "1b1c:1b20", "0", issue(45), USBHID),
    "strafe_bios_keyboard": ("Corsair Strafe RGB, BIOS mode", "1b1c:1b20", "0", issue(45), USBHID),
    "bolt_rx_v501_keyboard": ("Logi Bolt receiver, firmware 5.01", "046d:c548", "0", issue(47), USBHID),
    "bolt_rx_v501_iface1": ("Logi Bolt receiver, firmware 5.01", "046d:c548", "1", issue(47, 118), USBHID),
    "apple_a1243_keyboard": ("Apple A1243 keyboard", "05ac:024f", "0", issue(48), "hid-decode"),
    "blackdiamond75_keyboard": ("Dry Studio Black Diamond 75", "05ac:0256", "0", issue(50), USBHID),
    "blackdiamond75_iface1": ("Dry Studio Black Diamond 75", "05ac:0256", "1", issue(50), USBHID),
    "blackdiamond75_vendor": ("Dry Studio Black Diamond 75", "05ac:0256", "2", issue(50), USBHID),
    "bitdo_retro_mouse": ("8BitDo Retro Mechanical Keyboard", "2dc8:5201", "0", issue(57, 295), USBHID),
    "bitdo_retro_vendor": ("8BitDo Retro Mechanical Keyboard", "2dc8:5201", "1", issue(57, 295), USBHID),
    "cherry_strait_consumer": ("Cherry Strait 3.0", "046a:0180", "1", issue(61), USBHID),
    "issue71_qmk_keyboard": ("custom QMK keyboard, HID 1.01", NG, "0", issue(71), USBHID),
    "issue71_qmk_iface1": ("custom QMK keyboard, HID 1.01", NG, "1", issue(71), USBHID),
    "zmk_corne": ("ZMK Corne", "1d50:615e", "0", issue(79), MAC),
    "roccat_kone_mouse": ("Roccat Kone 2016", "1e7d:2cf0", "0", issue(92), USBHID),
    "roccat_kone_keyboard": ("Roccat Kone 2016", "1e7d:2cf0", "1", issue(92), USBHID),
    "issue99_mouse": ("mouse, unnamed", NG, "0", issue(99), USBHID),
    "cherry_mw8_keyboard": ("Cherry MW 8 Advanced", "046a:0114", "0", issue(133), USBHID),
    "cherry_mw8c_keyboard": ("Cherry MW 8C Advanced", "046a:c117", "0", issue(133), USBHID),
    "fc660c_tmk_keyboard": ("Leopold FC660C on TMK", NG, "0", issue(142), USBHID),
    "fc660c_tmk_mouse": ("Leopold FC660C on TMK", NG, "1", issue(142), USBHID),
    "fc660c_tmk_consumer": ("Leopold FC660C on TMK", NG, "2", issue(142), USBHID),
    "fc660c_tmk_console": ("Leopold FC660C on TMK", NG, "3", issue(142), USBHID),
    "fc660c_tmk_nkro": ("Leopold FC660C on TMK", NG, "4", issue(142), USBHID),
    "unifying_rx_b_keyboard": ("Logitech Unifying receiver, second unit", "046d:c52b", "0", issue(150), USBHID),
    "unifying_rx_b_iface1": ("Logitech Unifying receiver, second unit", "046d:c52b", "1", issue(150), USBHID),
    "unifying_rx_b_hidpp": ("Logitech Unifying receiver, second unit", "046d:c52b", "2", issue(150), USBHID),
    "vial_qmk_iface2": ("vial-qmk keyboard", NG, "2", issue(151), USBHID),
    "adv360_pro": ("Kinesis Advantage360 Pro on ZMK", "1d50:615e", "0", issue(160), MAC),
    "mighty_mouse": ("Apple Mighty Mouse", NG, "0", issue(185), "decoded listing, tool not stated"),
    "magic_trackpad_mouse": ("Apple Magic Trackpad", "05ac:0265", "mouse", issue(207), MAC),
    "magic_trackpad_ff0d": ("Apple Magic Trackpad", "05ac:0265", "vendor, usage 0x0D", issue(207), MAC),
    "magic_trackpad_ff03": ("Apple Magic Trackpad", "05ac:0265", "vendor, usage 3", issue(207), MAC),
    "keychron_dongle_vendor": ("Keychron 2.4 GHz dongle", NG, "1", issue(211), USBHID),
    "keychron_dongle_keyboard": ("Keychron 2.4 GHz dongle", NG, "2", issue(211), USBHID),
    "superlight2_wired_keyboard": ("Logitech G Pro Superlight 2, wired", NG, "1", issue(215), USBHID),
    "keyboardio_mouse": ("Keyboardio Model 100", NG, "4", issue(216), USBHID),
}

GD = {0x01: "pointer", 0x02: "mouse", 0x04: "joystick", 0x05: "gamepad", 0x06: "keyboard",
      0x07: "keypad", 0x08: "multi-axis", 0x80: "system"}


def collections(b):
    """Top-level collection kinds and every report ID, in declaration order."""
    kinds, rids, depth, page, usage = [], [], 0, None, None
    for item in hiditems.walk_items(b):
        if item.typ == 3:
            continue
        tag, data = item.tag, item.value
        if tag == 0x04:
            page = data
        elif tag == 0x08:
            usage = data
        elif tag == 0x84:
            if data not in rids:
                rids.append(data)
        elif tag == 0xA0:
            if depth == 0:
                if page == 0x01:
                    k = GD.get(usage, "generic desktop")
                elif page == 0x0C:
                    k = "consumer"
                elif page == 0x0D:
                    k = "digitizer"
                elif page is not None and page >= 0xFF00 or page in (0x8C, 0x0A, 0x0B):
                    k = "vendor"
                else:
                    k = "page %02X" % (page or 0)
                if k not in kinds:
                    kinds.append(k)
            depth += 1
        elif tag == 0xC0:
            depth -= 1
    return kinds, rids


def case_coverage():
    """entry -> set of suites with a hand-written table for it."""
    cov = {}
    for fn, tag, pat in (("cases_mouse.h", "mouse", r"DEV\((\w+),"),
                         ("cases_kbd.h", "kbd", r"DEV\((\w+),"),
                         ("cases_cc.h", "consumer", r"CCDEV\((\w+),")):
        text = open(os.path.join(ROOT, "src", fn)).read()
        for name in re.findall(pat, text):
            cov.setdefault(name, set()).add(tag)
    return cov


def order_key(name):
    src = META[name][3]
    m = re.search(r"#(\d+)", src)
    if m:
        return (0, int(m.group(1)), name)
    if src == SYN:
        return (3, 0, name)
    if src == HAND:
        return (2, 0, name)
    return (1, 0, name)


def table():
    corpus = ad.existing()
    missing = sorted(set(corpus) - set(META))
    stale = sorted(set(META) - set(corpus))
    if missing or stale:
        raise SystemExit("corpus_table.py: META and descriptors.h disagree\n"
                         "  entries without a row: %s\n  rows without an entry: %s"
                         % (", ".join(missing) or "-", ", ".join(stale) or "-"))
    cov = case_coverage()
    rows = ["| entry | device | VID:PID | interface | from | read with | bytes | declares | report IDs | decode cases |",
            "|---|---|---|---|---|---|---|---|---|---|"]
    for name in sorted(corpus, key=order_key):
        dev, ids, iface, src, tool = META[name]
        kinds, rids = collections(corpus[name])
        rows.append("| `%s` | %s | %s | %s | %s | %s | %d | %s | %s | %s |" % (
            name, dev, ids or "-", iface or "-", src, tool, len(corpus[name]),
            ", ".join(kinds) or "-",
            ", ".join(("0x%02X" % r) if r > 9 else str(r) for r in rids) or "none",
            ", ".join(sorted(cov.get(name, ()))) or "-"))
    return "\n".join(rows) + "\n"


def main():
    text = open(CORPUS_MD).read()
    b, e = text.find(BEGIN), text.find(END)
    if b < 0 or e < 0:
        raise SystemExit("corpus_table.py: markers not found in CORPUS.md")
    new = text[:b + len(BEGIN)] + "\n" + table() + text[e:]
    if new != text:
        open(CORPUS_MD, "w").write(new)
    print("%d entries tabulated" % len(ad.existing()))


if __name__ == "__main__":
    sys.exit(main())
