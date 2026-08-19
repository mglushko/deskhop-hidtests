# Builds bitdo-emu.uf2: an RP2040 that presents itself as an 8BitDo Retro
# Mechanical Keyboard and types a fixed test line.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/deskhop-extended/pico-sdk
#   cmake --build build
#
# The report descriptor is regenerated from ../descriptors.h on every build, so
# the emulator and the host harness can never disagree about the device.
cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_FETCH_FROM_GIT off)
if(NOT DEFINED PICO_SDK_PATH)
  set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../../deskhop-extended/pico-sdk)
endif()
include(${PICO_SDK_PATH}/pico_sdk_import.cmake)

project(bitdo_emu C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(GEN_DESC ${CMAKE_CURRENT_BINARY_DIR}/bitdo_desc.h)
add_custom_command(
  OUTPUT  ${GEN_DESC}
  COMMAND ${CMAKE_COMMAND} -E env python3
          ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py
          ${CMAKE_CURRENT_LIST_DIR}/../descriptors.h
          d_bitdo_retro_iface2
          ${GEN_DESC}
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/gen_desc.py
          ${CMAKE_CURRENT_LIST_DIR}/../descriptors.h
  COMMENT "Extracting d_bitdo_retro_iface2 from the harness corpus"
  VERBATIM)
add_custom_target(bitdo_desc DEPENDS ${GEN_DESC})

add_executable(bitdo-emu main.c usb_descriptors.c)
add_dependencies(bitdo-emu bitdo_desc)

target_include_directories(bitdo-emu PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(bitdo-emu pico_stdlib tinyusb_device)

pico_add_extra_outputs(bitdo-emu)
