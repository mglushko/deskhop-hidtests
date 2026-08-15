/* Mouse decode cases, shared by mousetest and shortreport.
 *
 * Split out of mousetest.c so the two binaries cannot disagree about what a
 * device sends. shortreport replays each of these at every truncated length, so
 * a case added here is checked for both its decoded values and its behaviour on
 * a short report, without being written twice.
 *
 * Expected values are worked out by hand from the descriptor, so a case fails if
 * either the parser or the extraction changes meaning.
 */
#pragma once

#include "descriptors.h"

typedef struct {
    const char *what;
    /* 12, not 8: the Bolt receiver's mouse report is nine bytes - a report ID, a
       16-bit button field, 16-bit X and Y, then wheel and pan. */
    uint8_t     report[12];
    int         len;
    int32_t     x, y, wheel, pan, buttons;
} mouse_case_t;

typedef struct {
    const char         *name;
    const uint8_t      *desc;
    int                 desc_len;
    uint8_t             protocol;
    const mouse_case_t *cases;
    unsigned            count;
} mouse_device_t;

/* Gameball trackball, mi_00: no report ID, everything 8 bits, one 5-byte report. */
static const mouse_case_t m_gameball_cases[] = {
    {"ball right (X +20)",         {0x00, 0x14, 0x00, 0x00, 0x00}, 5,   20,    0,  0,  0,  0},
    {"ball left  (X -20)",         {0x00, 0xEC, 0x00, 0x00, 0x00}, 5,  -20,    0,  0,  0,  0},
    {"ball down  (Y +20)",         {0x00, 0x00, 0x14, 0x00, 0x00}, 5,    0,   20,  0,  0,  0},
    {"ball up    (Y -20)",         {0x00, 0x00, 0xEC, 0x00, 0x00}, 5,    0,  -20,  0,  0,  0},
    {"ball diagonal up-left",      {0x00, 0xF6, 0xF6, 0x00, 0x00}, 5,  -10,  -10,  0,  0,  0},
    {"ball fast (X +127)",         {0x00, 0x7F, 0x00, 0x00, 0x00}, 5,  127,    0,  0,  0,  0},
    {"ball fast (X -128)",         {0x00, 0x80, 0x00, 0x00, 0x00}, 5, -128,    0,  0,  0,  0},

    {"side pad: scroll up",        {0x00, 0x00, 0x00, 0x01, 0x00}, 5,    0,    0,  1,  0,  0},
    {"side pad: scroll down",      {0x00, 0x00, 0x00, 0xFF, 0x00}, 5,    0,    0, -1,  0,  0},
    {"side pad: scroll down fast", {0x00, 0x00, 0x00, 0xFB, 0x00}, 5,    0,    0, -5,  0,  0},
    {"side pad: pan right",        {0x00, 0x00, 0x00, 0x00, 0x01}, 5,    0,    0,  0,  1,  0},
    {"side pad: pan left",         {0x00, 0x00, 0x00, 0x00, 0xFF}, 5,    0,    0,  0, -1,  0},
    {"both pads at once",          {0x00, 0x00, 0x00, 0x02, 0xFE}, 5,    0,    0,  2, -2,  0},

    {"button 1 (left)",            {0x01, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  1},
    {"button 2 (right)",           {0x02, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  2},
    {"button 3 (middle)",          {0x04, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  4},
    {"button 4",                   {0x08, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  8},
    {"button 5",                   {0x10, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0, 16},
    {"all five buttons",           {0x1F, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0, 31},

    {"drag: btn1 + move",          {0x01, 0x0A, 0xF6, 0x00, 0x00}, 5,   10,  -10,  0,  0,  1},
    {"btn3 + scroll",              {0x04, 0x00, 0x00, 0x03, 0x00}, 5,    0,    0,  3,  0,  4},
    {"everything at once",         {0x1F, 0x7F, 0x81, 0x02, 0xFE}, 5,  127, -127,  2, -2, 31},
};

/* Kensington Expert Mouse, mi_00 (issue #218), and byte for byte the same layout
   on the Cherry MW 8C, mi_01 (issue #133).
     report 1: [id][buttons 5 bits + 3 pad][wheel][pan]
     report 2: [id][X 12 bits][Y 12 bits], packed low nibble first:
               byte 1 = X & 0xFF, byte 2 = (X >> 8) | ((Y & 0xF) << 4), byte 3 = Y >> 4
   extract_value() skips a field whose report_id does not match the report in
   hand, so a report 1 leaves X and Y at zero and a report 2 leaves wheel and pan
   at zero. Buttons are the exception: when skipped they fall back to the last
   known state->mouse_buttons, which is zero throughout this test. */
static const mouse_case_t m_kensington_cases[] = {
    {"r1: button 1 (left)",     {0x01, 0x01, 0x00, 0x00}, 4,     0,     0,  0,  0,  1},
    {"r1: button 2 (right)",    {0x01, 0x02, 0x00, 0x00}, 4,     0,     0,  0,  0,  2},
    {"r1: all five buttons",    {0x01, 0x1F, 0x00, 0x00}, 4,     0,     0,  0,  0, 31},
    {"r1: scroll up",           {0x01, 0x00, 0x01, 0x00}, 4,     0,     0,  1,  0,  0},
    {"r1: scroll down",         {0x01, 0x00, 0xFF, 0x00}, 4,     0,     0, -1,  0,  0},
    {"r1: pan right",           {0x01, 0x00, 0x00, 0x01}, 4,     0,     0,  0,  1,  0},
    {"r1: pan left",            {0x01, 0x00, 0x00, 0xFF}, 4,     0,     0,  0, -1,  0},
    {"r1: btn3 + scroll",       {0x01, 0x04, 0x03, 0x00}, 4,     0,     0,  3,  0,  4},

    {"r2: ball right (X +1)",   {0x02, 0x01, 0x00, 0x00}, 4,     1,     0,  0,  0,  0},
    {"r2: ball left  (X -1)",   {0x02, 0xFF, 0x0F, 0x00}, 4,    -1,     0,  0,  0,  0},
    {"r2: ball down  (Y +1)",   {0x02, 0x00, 0x10, 0x00}, 4,     0,     1,  0,  0,  0},
    {"r2: ball up    (Y -1)",   {0x02, 0x00, 0xF0, 0xFF}, 4,     0,    -1,  0,  0,  0},
    {"r2: X +2047 (max)",       {0x02, 0xFF, 0x07, 0x00}, 4,  2047,     0,  0,  0,  0},
    {"r2: X -2047 (min)",       {0x02, 0x01, 0x08, 0x00}, 4, -2047,     0,  0,  0,  0},
    {"r2: Y +2047 (max)",       {0x02, 0x00, 0xF0, 0x7F}, 4,     0,  2047,  0,  0,  0},
    {"r2: Y -2047 (min)",       {0x02, 0x00, 0x10, 0x80}, 4,     0, -2047,  0,  0,  0},
    {"r2: X +100, Y -100",      {0x02, 0x64, 0xC0, 0xF9}, 4,   100,  -100,  0,  0,  0},
    {"r2: X sign bit only",     {0x02, 0x00, 0x08, 0x00}, 4, -2048,     0,  0,  0,  0},
};

/* Cherry MW 8 Advanced, mi_01 (issue #133), the model that works. One report
   carries the lot, so Y begins at bit 20:
     report 3: [id][buttons 5 bits + 3 pad][X 12 bits][Y 12 bits][wheel][pan]
               byte 2 = X & 0xFF, byte 3 = (X >> 8) | ((Y & 0xF) << 4), byte 4 = Y >> 4 */
static const mouse_case_t m_cherry_mw8_cases[] = {
    {"button 1 (left)",      {0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0,  1},
    {"all five buttons",     {0x03, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0, 31},
    {"ball right (X +1)",    {0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 7,     1,     0,    0,    0,  0},
    {"ball left  (X -1)",    {0x03, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}, 7,    -1,     0,    0,    0,  0},
    {"ball down  (Y +1)",    {0x03, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}, 7,     0,     1,    0,    0,  0},
    {"ball up    (Y -1)",    {0x03, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00}, 7,     0,    -1,    0,    0,  0},
    {"X +2047, Y -2047",     {0x03, 0x00, 0xFF, 0x17, 0x80, 0x00, 0x00}, 7,  2047, -2047,    0,    0,  0},
    {"X -2047, Y +2047",     {0x03, 0x00, 0x01, 0xF8, 0x7F, 0x00, 0x00}, 7, -2047,  2047,    0,    0,  0},
    {"scroll up",            {0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 7,     0,     0,    1,    0,  0},
    {"scroll down",          {0x03, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 7,     0,     0,   -1,    0,  0},
    {"pan right",            {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 7,     0,     0,    0,    1,  0},
    {"pan left",             {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 7,     0,     0,    0,   -1,  0},
    {"drag: btn1 + move",    {0x03, 0x01, 0x0A, 0x60, 0xFF, 0x00, 0x00}, 7,    10,   -10,    0,    0,  1},
    {"everything at once",   {0x03, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,  2047, -2047,  127, -127, 31},
};

/* Logitech MX518. No report ID. The two vendor bytes at 1 and 2 are declared
   inside the mouse collection but belong to no usage the parser tracks, so the
   axes sit further along than a naive reading suggests:
     [buttons 8][vendor][vendor][wheel 8][X 12 bits][Y 12 bits]
   X starts at bit 32 and Y at bit 44, so byte 5 carries the top nibble of X in
   its low half and the bottom nibble of Y in its high half:
     byte 4 = X & 0xFF, byte 5 = (X >> 8) | ((Y & 0xF) << 4), byte 6 = Y >> 4
   This device has no AC Pan, so pan stays 0 throughout. */
static const mouse_case_t m_mx518_cases[] = {
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0, 0,   1},
    /* -1, not 255: get_report_value sign-extends, and this is the first mouse in
       the corpus with 8 buttons, so it is the first whose button field can set
       bit 7. Harmless on the wire - mouse_report_t.buttons is uint8_t, so the
       low 8 bits ship as 0xFF either way - but it is why the expected value
       here is not the 255 you would write down from the descriptor alone. */
    {"all eight buttons",     {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0, 0,  -1},
    {"move right (X +1)",     {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}, 7,     1,     0,    0, 0,   0},
    {"move left  (X -1)",     {0x00, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x00}, 7,    -1,     0,    0, 0,   0},
    {"move down  (Y +1)",     {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00}, 7,     0,     1,    0, 0,   0},
    {"move up    (Y -1)",     {0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xFF}, 7,     0,    -1,    0, 0,   0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 7,     0,     0,    1, 0,   0},
    {"scroll down",           {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, 7,     0,     0,   -1, 0,   0},
    {"X +2047, Y -2047",      {0x00, 0x00, 0x00, 0x00, 0xFF, 0x17, 0x80}, 7,  2047, -2047,    0, 0,   0},
    {"X -2047, Y +2047",      {0x00, 0x00, 0x00, 0x00, 0x01, 0xF8, 0x7F}, 7, -2047,  2047,    0, 0,   0},
    {"drag: btn1 + move",     {0x01, 0x00, 0x00, 0x00, 0x0A, 0x60, 0xFF}, 7,    10,   -10,    0, 0,   1},
    /* the point of this one: vendor bytes full of noise must not reach any axis */
    {"vendor bytes ignored",  {0x00, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x00}, 7,     1,     0,    0, 0,   0},
    {"everything at once",    {0xFF, 0xAA, 0x55, 0x7F, 0xFF, 0x17, 0x80}, 7,  2047, -2047,  127, 0,  -1},
};

/* Kernel docs multi-collection device, decoded against report ID 2 - the second
   mouse collection, which is the one left standing in iface->mouse after the
   parser walks both. Layout after the ID byte:
     [buttons 5 + 3 pad][X 12 bits][Y 12 bits][wheel 8][pan 8]
   The last case feeds report ID 1, the first collection, and expects nothing to
   come out. That is not a typo. extract_value bails when the report's leading ID
   byte does not equal mouse->report_id, and the second collection overwrote
   report_id with 2 as the parser walked past it, so every field of an ID 1
   report fails the check and the values stay zero. usb.c still routes those
   reports here, because report_handler[1] was bound while the first collection
   was being parsed - so they arrive at the mouse path and are silently dropped.
   Both collections happen to declare the same layout, so nothing would have been
   lost by decoding ID 1 with ID 2's offsets; the parser just has no way to do
   that with one mouse_t per interface. */
static const mouse_case_t m_kernel_multi_cases[] = {
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0,  1},
    {"all five buttons",      {0x02, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0, 31},
    {"move right (X +1)",     {0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 7,     1,     0,    0,    0,  0},
    {"move left  (X -1)",     {0x02, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}, 7,    -1,     0,    0,    0,  0},
    {"move down  (Y +1)",     {0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}, 7,     0,     1,    0,    0,  0},
    {"move up    (Y -1)",     {0x02, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00}, 7,     0,    -1,    0,    0,  0},
    {"X +2047, Y -2047",      {0x02, 0x00, 0xFF, 0x17, 0x80, 0x00, 0x00}, 7,  2047, -2047,    0,    0,  0},
    {"scroll up",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 7,     0,     0,    1,    0,  0},
    {"scroll down",           {0x02, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 7,     0,     0,   -1,    0,  0},
    {"pan right",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 7,     0,     0,    0,    1,  0},
    {"pan left",              {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 7,     0,     0,    0,   -1,  0},
    {"everything at once",    {0x02, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,  2047, -2047,  127, -127, 31},
    {"report 1 dropped",      {0x01, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,     0,     0,    0,    0,  0},
};

/* Logi Bolt receiver, interface 1. Nine-byte report: ID 2, a 16-bit button field,
   16-bit X and Y, then 8-bit wheel and pan. The first device in the corpus with
   more than eight buttons, which is what makes the last two rows worth having:
   get_report_value() sign-extends on the top bit of the field, so a 16-bit button
   bitmap with bit 15 set comes back negative. Harmless downstream only because
   mouse_report_t.buttons is a uint8_t and buttons 9-16 are dropped there anyway. */
static const mouse_case_t m_bolt_rx_cases[] = {
    {"move right (X +20)",    {0x02, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,   20,   0,   0,   0,      0},
    {"move left  (X -20)",    {0x02, 0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00, 0x00, 0x00}, 9,  -20,   0,   0,   0,      0},
    {"move down  (Y +20)",    {0x02, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, 9,    0,  20,   0,   0,      0},
    {"move up    (Y -20)",    {0x02, 0x00, 0x00, 0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00}, 9,    0, -20,   0,   0,      0},
    {"X +32767, Y -32767",    {0x02, 0x00, 0x00, 0xFF, 0x7F, 0x01, 0x80, 0x00, 0x00}, 9, 32767, -32767, 0, 0,      0},
    {"scroll up",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 9,    0,   0,   1,   0,      0},
    {"scroll down",           {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 9,    0,   0,  -1,   0,      0},
    {"pan right",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 9,    0,   0,   0,   1,      0},
    {"pan left",              {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 9,    0,   0,   0,  -1,      0},
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,    0,   0,   0,   0,      1},
    {"button 8, still positive", {0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9, 0,   0,   0,   0,    128},
    /* bit 15 set: the sign extension the README's button finding is about */
    {"button 16 alone",       {0x02, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,    0,   0,   0,   0, -32768},
    {"all 16 buttons held",   {0x02, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,    0,   0,   0,   0,     -1},
};

/* Keychron Ultra-Link 8K, interface 0. Eight-byte report on ID 1: five buttons
   padded to a byte, then 16-bit X and Y, wheel and pan. */
static const mouse_case_t m_ultralink_cases[] = {
    {"move right (X +20)",    {0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,   20,   0,   0,   0,  0},
    {"move left  (X -20)",    {0x01, 0x00, 0xEC, 0xFF, 0x00, 0x00, 0x00, 0x00}, 8,  -20,   0,   0,   0,  0},
    {"move down  (Y +20)",    {0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, 8,    0,  20,   0,   0,  0},
    {"move up    (Y -20)",    {0x01, 0x00, 0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00}, 8,    0, -20,   0,   0,  0},
    {"scroll up",             {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 8,    0,   0,   1,   0,  0},
    {"pan right",             {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,    0,   0,   0,   1,  0},
    {"all five buttons",      {0x01, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,    0,   0,   0,   0, 31},
    {"everything at once",    {0x01, 0x1F, 0xFF, 0x7F, 0x01, 0x80, 0x7F, 0x81}, 8, 32767, -32767, 127, -127, 31},
};

/* Logi Bolt receiver, interface 3: a Precision Touchpad. It declares Generic
   Desktop X and Y, but inside a Digitizer top-level collection, so global_usage is
   0x05 and no entry in extract_data()'s map matches. Nothing is found, no handler
   is bound, and a finger report decodes to zeros. Asserting the absence, the same
   way the mx518 case asserts vendor bytes never reach an axis. */
static const mouse_case_t m_bolt_touchpad_cases[] = {
    {"finger down, absolute X/Y", {0x28, 0x03, 0x01, 0x40, 0xD7, 0x0A, 0xFA, 0x06}, 8, 0, 0, 0, 0, 0},
};

/* The boot-protocol path, which kbdtest has had for keyboards all along and this
   file has not had for mice. extract_report_values() returns early when the
   protocol is BOOT and reads the bytes through a hid_mouse_report_t * - buttons,
   x, y, wheel, pan - without consulting the descriptor and without looking at
   len. d_boot_mouse's own layout is exactly that, so a full 5-byte report decodes
   correctly here; the point of the entry is that shortreport can then hand the
   same path a report shorter than the struct. */
static const mouse_case_t m_boot_protocol_cases[] = {
    {"boot: move right (X +20)", {0x00, 0x14, 0x00, 0x00, 0x00}, 5,   20,   0,  0,  0,  0},
    {"boot: move up-left",       {0x00, 0xF6, 0xF6, 0x00, 0x00}, 5,  -10, -10,  0,  0,  0},
    {"boot: scroll and pan",     {0x00, 0x00, 0x00, 0x01, 0xFF}, 5,    0,   0,  1, -1,  0},
    {"boot: all three buttons",  {0x07, 0x00, 0x00, 0x00, 0x00}, 5,    0,   0,  0,  0,  7},
};

#define DEV(d, p, c) {#d, d_##d, (int)sizeof(d_##d), p, c, (unsigned)ARRAY_SIZE(c)}

static const mouse_device_t mouse_devices[] = {
    DEV(gameball_trackball, HID_PROTOCOL_REPORT, m_gameball_cases),
    DEV(kensington_expert_mouse, HID_PROTOCOL_REPORT, m_kensington_cases),
    DEV(cherry_mw8c_mouse, HID_PROTOCOL_REPORT, m_kensington_cases),
    DEV(cherry_mw8_mouse, HID_PROTOCOL_REPORT, m_cherry_mw8_cases),
    DEV(mx518_mouse, HID_PROTOCOL_REPORT, m_mx518_cases),
    DEV(kernel_multi_collection, HID_PROTOCOL_REPORT, m_kernel_multi_cases),
    DEV(bolt_rx_iface1, HID_PROTOCOL_REPORT, m_bolt_rx_cases),
    DEV(ultralink_mouse, HID_PROTOCOL_REPORT, m_ultralink_cases),
    DEV(bolt_rx_touchpad, HID_PROTOCOL_REPORT, m_bolt_touchpad_cases),
    DEV(boot_mouse, HID_PROTOCOL_BOOT, m_boot_protocol_cases),
};

#undef DEV
