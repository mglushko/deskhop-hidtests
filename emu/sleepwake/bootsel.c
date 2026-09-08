/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Adapted from pico-examples/picoboard/button/button.c. See LICENSE.bootsel.
 * This target never starts core 1 or DMA readers of flash. Do not reuse this
 * routine in a multicore program without coordinating every other flash reader.
 */
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

bool __no_inline_not_in_flash_func(sleepwake_bootsel_pressed)(void) {
    const uint cs = 1;
    uint32_t flags = save_and_disable_interrupts();
    uint32_t saved_ctrl = ioqspi_hw->io[cs].ctrl;
    hw_write_masked(&ioqspi_hw->io[cs].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    /* Flash is unavailable here, including flash-resident sleep functions. */
    for (volatile int i = 0; i < 1000; ++i) {}
    bool pressed = !(sio_hw->gpio_hi_in & (1u << cs));
    ioqspi_hw->io[cs].ctrl = saved_ctrl;
    restore_interrupts(flags);
    return pressed;
}
