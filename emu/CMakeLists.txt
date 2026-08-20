# Builds one UF2 per emulated device:
#
#   bitdo-emu.uf2     8BitDo Retro Mechanical Keyboard, hrvach/deskhop#57
#   gameball-emu.uf2  Gameball trackball 0782:001B, hrvach/deskhop#332
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

  add_custom_command(
    OUTPUT  ${header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${gendir}
    COMMAND ${CMAKE_COMMAND} -E env python3
            ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py ${CORPUS} ${header} ${ARGN}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py ${CORPUS}
    COMMENT "Extracting ${target} descriptors from the harness corpus"
    VERBATIM)
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
