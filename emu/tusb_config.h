/* Device-only TinyUSB config. The emulator is purely a USB device: it presents
   one HID interface carrying the 8BitDo's report descriptor and nothing else. */
#pragma once

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO
#define CFG_TUD_ENABLED         1
#define CFG_TUH_ENABLED         0

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_HID             1
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* Must hold the largest report plus its ID byte: the NKRO collections are
   16 payload bytes + 1 = 17. 64 leaves room to change the script. */
#define CFG_TUD_HID_EP_BUFSIZE  64
