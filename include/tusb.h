/* Shim. deskhop's hid_parser.h does #include "tusb.h"; this include directory is
   searched before the target's, so that resolves here instead of pulling in the
   real TinyUSB. Everything the two files under test need is in harness.h. */
#pragma once

#include "harness.h"
