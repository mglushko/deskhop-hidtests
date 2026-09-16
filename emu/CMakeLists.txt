# Builds one UF2 per emulated device, and four more for the Gameball:
#
#   bitdo-emu.uf2           8BitDo Retro Mechanical Keyboard, hrvach/deskhop#57
#   ultralink-emu.uf2       Keychron Ultra-Link 8K 3434:d028, hrvach/deskhop#324
#   gameball-emu.uf2        Gameball trackball 0782:001B, hrvach/deskhop#332
#   gameball-min-emu.uf2      the trackball interface alone, bytes as dumped
#   gameball-c0-emu.uf2       trackball alone, End Collection rewritten to 0xC0
#   gameball-fixed-emu.uf2    trackball alone, both descriptor repairs
#   gameball-full-emu.uf2     all three interfaces, both repairs: the #332 test
#   sculpt-emu.uf2          Microsoft Sculpt receiver 045e:07a5, hrvach/deskhop#367
#   sleepwake-emu.uf2       BOOTSEL-triggered System Sleep/Wake, deskhop-extended#22/#25
#
# From the harness root:
#
#   cmake -S emu -B emu/build -DPICO_SDK_PATH=$HOME/deskhop-extended/pico-sdk \
#       -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
#   cmake --build emu/build
#
# Report descriptors are regenerated from ../descriptors.h on every build, so an
# emulator and the host harness can never disagree about the device. Each target
# gets its own generated header holding only its own descriptors.
cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_FETCH_FROM_GIT off)
if(NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../../deskhop-extended/pico-sdk)
endif()
include(${PICO_SDK_PATH}/pico_sdk_import.cmake)

project(deskhop_emu C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(CORPUS ${CMAKE_CURRENT_LIST_DIR}/../descriptors.h)

# emu_device(<target> <defines> DESC <c_name>=<prefix> ... [--flag ...]
#            [SOURCES <file> ...] [LIBS <lib> ...])
#
# One generated header per target, holding only that target's descriptors, then the
# executable. SOURCES defaults to the two files the timed rigs share; the Sleep/Wake rig
# brings its own, and the library its BOOTSEL sampler needs.
function(emu_device target defines)
  cmake_parse_arguments(ARG "" "" "DESC;SOURCES;LIBS" ${ARGN})
  if(NOT ARG_DESC)
    message(FATAL_ERROR "${target}: no DESC <c_name>=<prefix> given")
  endif()
  if(NOT ARG_SOURCES)
    set(ARG_SOURCES main.c usb_descriptors.c)
  endif()

  set(gendir ${CMAKE_CURRENT_BINARY_DIR}/gen/${target})
  set(header ${gendir}/emu_desc.h)

  add_custom_command(
    OUTPUT  ${header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${gendir}
    COMMAND ${CMAKE_COMMAND} -E env python3
            ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py ${CORPUS} ${header} ${ARG_DESC}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py
            ${CMAKE_CURRENT_LIST_DIR}/../tools/hiditems.py ${CORPUS}
    COMMENT "Extracting ${target} descriptors from the harness corpus"
    VERBATIM)
  add_custom_target(${target}_desc DEPENDS ${header})

  add_executable(${target} ${ARG_SOURCES})
  add_dependencies(${target} ${target}_desc)
  if(defines)
    target_compile_definitions(${target} PRIVATE ${defines})
  endif()
  target_compile_options(${target} PRIVATE -Wall -Wextra)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${gendir})
  target_link_libraries(${target} pico_stdlib tinyusb_device ${ARG_LIBS})
  pico_add_extra_outputs(${target})
endfunction()

emu_device(bitdo-emu "EMU_BITDO"
  DESC d_bitdo_retro_iface2=bitdo)

emu_device(ultralink-emu "EMU_ULTRALINK"
  DESC d_ultralink_iface1=ultralink)

# All three interfaces of the receiver, in its order. Only the mouse is the test,
# but the keyboard carries the positive control and the consumer interface is what
# makes the endpoint layout the real one.
emu_device(sculpt-emu "EMU_SCULPT"
  DESC d_sculpt_rx_keyboard=sculpt_keyboard
  d_sculpt_rx_mouse=sculpt_mouse
  d_sculpt_rx_consumer=sculpt_consumer)

emu_device(gameball-emu "EMU_GAMEBALL"
  DESC d_gameball_trackball=gameball_trackball
  d_gameball_gesture=gameball_gesture
  d_gameball_keyboard=gameball_keyboard)

# Bisect build: the trackball interface alone. Same motion, same LED, so a run
# against this one isolates whether presenting the other two interfaces is what
# stops the pointer moving.
emu_device(gameball-min-emu "EMU_GAMEBALL;EMU_MINIMAL"
  DESC d_gameball_trackball=gameball_trackball)

# Same again, but with End Collection rewritten to the well formed 0xC0. Every
# Gameball descriptor in the corpus uses 0xC1 0x00, which is bSize 1 where the
# spec says 0. This build is what tells you whether a strict host is refusing the
# descriptor over that, rather than over anything the emulator is doing.
emu_device(gameball-c0-emu "EMU_GAMEBALL;EMU_NORMALISED"
  DESC d_gameball_trackball=gameball_trackball --normalise-end-collection)

# Both repairs. On top of End Collection, the axes get a negative Logical
# Minimum: the corpus descriptor never sets one, so the 0 left over from the
# button block stands and X, Y, wheel and pan read as unsigned 0..127. A host
# that honours that throws away every negative delta and the pointer can only
# travel right and down.
emu_device(gameball-fixed-emu "EMU_GAMEBALL;EMU_FIXED"
  DESC d_gameball_trackball=gameball_trackball --normalise-end-collection --fix-signed-axes)

# The one that tests #332. All three interfaces, both repairs, so the gesture
# interface with its Report Counts of 256, 1024 and 2048 is actually presented to
# deskhop while the trackball stays watchable. The repairs are applied because a
# host has to accept the device before any of this can be tested at all, and the
# evidence says they bring the bytes closer to what the real device sends rather
# than further from it.
emu_device(gameball-full-emu "EMU_GAMEBALL;EMU_FULLFIX"
  DESC d_gameball_trackball=gameball_trackball
  d_gameball_gesture=gameball_gesture
  d_gameball_keyboard=gameball_keyboard
  --normalise-end-collection --fix-signed-axes)

# An interactive rig with its own main loop and sources; BOOTSEL sampling is specific
# to the RP2040. Same descriptor generation and build as the timed rigs above, plus
# -Werror, which this rig has always been held to.
emu_device(sleepwake-emu ""
  DESC d_sleepwake_emu=sleepwake
  SOURCES sleepwake/main.c sleepwake/control.c sleepwake/bootsel.c sleepwake/usb_descriptors.c
  LIBS hardware_sync)
target_compile_options(sleepwake-emu PRIVATE -Werror)
