/* Device-only TinyUSB config. The emulator is purely a USB device: it presents
   one HID interface carrying the 8BitDo's report descriptor and nothing else. */
#pragma once

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO
#define CFG_TUD_ENABLED         1
#define CFG_TUH_ENABLED         0

/* Both of these are required, and the second one is the trap. CFG_TUD_ENABLED
   alone compiles every device class driver, so the build looks complete and the
   binary links, but tusb_init() is guarded on

       #if CFG_TUD_ENABLED && defined(TUD_OPT_RHPORT)

   and TUD_OPT_RHPORT is only defined by tusb_option.h when CFG_TUSB_RHPORT0_MODE
   carries OPT_MODE_DEVICE. Without it tusb_init() compiles to "return true" and
   the device stack is never brought up: the board powers on, runs, blinks, and
   never enumerates. main.c has a #error that catches this at build time. */
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

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
