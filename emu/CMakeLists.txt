# Builds one UF2 per emulated device:
#
#   bitdo-emu.uf2     8BitDo Retro Mechanical Keyboard, hrvach/deskhop#57
#   gameball-emu.uf2  Gameball trackball 0782:001B, hrvach/deskhop#332
#   poll-10-emu.uf2   a mouse asking for 10 ms polling, hrvach/deskhop#215
#   poll-1-emu.uf2    the same mouse asking for 1 ms
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/deskhop-extended/pico-sdk
#   cmake --build build
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

# emu_device(<target> <define> <c_name>=<prefix> ...)
function(emu_device target defines)
  set(gendir ${CMAKE_CURRENT_BINARY_DIR}/gen/${target})
  set(header ${gendir}/emu_desc.h)

  # A target that names no corpus descriptors gets an empty header rather than a run of
  # gen_desc.py with nothing to do. The script refuses that case on purpose, since for
  # every device here it would mean the pairs were forgotten; the polling pair is the one
  # target that legitimately has none, because it stands for no real device.
  if(ARGN)
    add_custom_command(
      OUTPUT  ${header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${gendir}
      COMMAND ${CMAKE_COMMAND} -E env python3
              ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py ${CORPUS} ${header} ${ARGN}
      DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py ${CORPUS}
      COMMENT "Extracting ${target} descriptors from the harness corpus"
      VERBATIM)
  else()
    add_custom_command(
      OUTPUT  ${header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${gendir}
      COMMAND ${CMAKE_COMMAND} -E echo "/* No corpus descriptors: this target stands for no real device. */" > ${header}
      COMMENT "No corpus descriptors for ${target}"
      VERBATIM)
  endif()
  add_custom_target(${target}_desc DEPENDS ${header})

  add_executable(${target} main.c usb_descriptors.c)
  add_dependencies(${target} ${target}_desc)
  target_compile_definitions(${target} PRIVATE ${defines})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${gendir})
  target_link_libraries(${target} pico_stdlib tinyusb_device)
  pico_add_extra_outputs(${target})
endfunction()

emu_device(bitdo-emu "EMU_BITDO"
  d_bitdo_retro_iface2=bitdo)

emu_device(gameball-emu "EMU_GAMEBALL"
  d_gameball_trackball=gameball_trackball
  d_gameball_gesture=gameball_gesture
  d_gameball_keyboard=gameball_keyboard)

# Bisect build: the trackball interface alone. Same motion, same LED, so a run
# against this one isolates whether presenting the other two interfaces is what
# stops the pointer moving.
emu_device(gameball-min-emu "EMU_GAMEBALL;EMU_MINIMAL"
  d_gameball_trackball=gameball_trackball)

# Same again, but with End Collection rewritten to the well formed 0xC0. Every
# Gameball descriptor in the corpus uses 0xC1 0x00, which is bSize 1 where the
# spec says 0. This build is what tells you whether a strict host is refusing the
# descriptor over that, rather than over anything the emulator is doing.
emu_device(gameball-c0-emu "EMU_GAMEBALL;EMU_NORMALISED"
  d_gameball_trackball=gameball_trackball --normalise-end-collection)

# Both repairs. On top of End Collection, the axes get a negative Logical
# Minimum: the corpus descriptor never sets one, so the 0 left over from the
# button block stands and X, Y, wheel and pan read as unsigned 0..127. A host
# that honours that throws away every negative delta and the pointer can only
# travel right and down.
emu_device(gameball-fixed-emu "EMU_GAMEBALL;EMU_FIXED"
  d_gameball_trackball=gameball_trackball --normalise-end-collection --fix-signed-axes)

# The one that tests #332. All three interfaces, both repairs, so the gesture
# interface with its Report Count of 16328 is actually presented to deskhop while
# the trackball stays watchable. The repairs are applied because a host has to
# accept the device before any of this can be tested at all, and the evidence
# says they bring the bytes closer to what the real device sends rather than
# further from it.
emu_device(gameball-full-emu "EMU_GAMEBALL;EMU_FULLFIX"
  d_gameball_trackball=gameball_trackball
  d_gameball_gesture=gameball_gesture
  d_gameball_keyboard=gameball_keyboard
  --normalise-end-collection --fix-signed-axes)

# The pair for the polling interval, hrvach/deskhop#215. The same plain relative mouse
# twice, differing only in the bInterval its endpoint asks for, sending a report every
# time it is polled so the rate the host sees is the polling rate.
#
#   poll-10-emu   asks for 10 ms, what a Lightspeed receiver asks for behind DeskHop
#   poll-1-emu    asks for 1 ms, the full speed ceiling
#
# On a board that honours what was asked, the first measures ~100 reports/s and the
# second ~1000. On a board that clamps, both measure ~1000. Neither takes a descriptor
# from the corpus: there is no real device here, and the report descriptor is TinyUSB's
# stock mouse precisely so that nothing about it can confound the measurement.
emu_device(poll-10-emu "EMU_POLL=10")
emu_device(poll-1-emu  "EMU_POLL=1")
