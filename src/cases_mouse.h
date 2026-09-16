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
#include "kept_out.h"

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
   button state known for this device, which is zero throughout this test - so the
   r2 cases below read the same on either side of that fallback changing where it
   looks. Where it looks is the subject of run_button_fallback() in mousetest.c. */
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


/* Microsoft Sculpt Ergonomic Mouse receiver, interface 1 (issue #367). Every report
   below is one the reporter captured with usbhid-dump, so the expected values are
   read off the wire rather than derived. Layout is [0x1A][5 buttons + 3 pad][X 16]
   [Y 16][wheel 16][pan 16], ten bytes. The wheel reads 12 per notch in the capture
   because the Linux host had set the Resolution Multiplier feature; a host that never
   touches that feature gets the device default instead. Decoding the bytes is one
   question and routing the report is another: 0x1A is 26, above MAX_REPORTS, so see
   dispatchtest for what happens to the report before any of this runs. */
static const mouse_case_t m_sculpt_cases[] = {
    {"move left  (X -1)",       {0x1A, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,  -1,  0,   0,  0, 0},
    {"move right (X +1)",       {0x1A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   1,  0,   0,  0, 0},
    {"move up    (Y -1)",       {0x1A, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00}, 10,   0, -1,   0,  0, 0},
    {"move down  (Y +1)",       {0x1A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   0,  1,   0,  0, 0},
    {"left button",             {0x1A, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   0,  0,   0,  0, 1},
    {"right button",            {0x1A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   0,  0,   0,  0, 2},
    {"middle button",           {0x1A, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   0,  0,   0,  0, 4},
    {"side button (back)",      {0x1A, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 10,   0,  0,   0,  0, 8},
    {"wheel up   (+12)",        {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00}, 10,   0,  0,  12,  0, 0},
    {"wheel down (-12)",        {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF4, 0xFF, 0x00, 0x00}, 10,   0,  0, -12,  0, 0},
    {"tilt left  (pan -3)",     {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xFF}, 10,   0,  0,   0, -3, 0},
    {"tilt right (pan +3)",     {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00}, 10,   0,  0,   0,  3, 0},
    {"synthetic: extremes",     {0x1A, 0x1F, 0xFF, 0x7F, 0x01, 0x80, 0x7F, 0x81, 0x01, 0x80}, 10, 32767, -32767, -32385, -32767, 31},
};

/* Logitech G502 on its cable, [#17]. Eight bytes and no report ID: a 16-bit button
   field, 16-bit X and Y, then 8-bit wheel and pan - the Bolt receiver's layout without
   the ID byte, so bit 15 of the buttons sign-extends here just the same. */
static const mouse_case_t m_g502_cases[] = {
    {"move right (X +20)",    {0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,    20,      0,   0,   0,      0},
    {"move left  (X -20)",    {0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00, 0x00, 0x00}, 8,   -20,      0,   0,   0,      0},
    {"move down  (Y +20)",    {0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, 8,     0,     20,   0,   0,      0},
    {"move up    (Y -20)",    {0x00, 0x00, 0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00}, 8,     0,    -20,   0,   0,      0},
    {"X +32767, Y -32767",    {0x00, 0x00, 0xFF, 0x7F, 0x01, 0x80, 0x00, 0x00}, 8, 32767, -32767,   0,   0,      0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 8,     0,      0,   1,   0,      0},
    {"scroll down",           {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 8,     0,      0,  -1,   0,      0},
    {"pan right",             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,     0,      0,   0,   1,      0},
    {"pan left",              {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 8,     0,      0,   0,  -1,      0},
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,      0,   0,   0,      1},
    {"button 8, still positive", {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,  0,      0,   0,   0,    128},
    {"button 16 alone",       {0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,      0,   0,   0, -32768},
    {"all 16 buttons held",   {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,      0,   0,   0,     -1},
    {"drag: btn1 + move",     {0x01, 0x00, 0x0A, 0x00, 0xF6, 0xFF, 0x00, 0x00}, 8,    10,    -10,   0,   0,      1},
};

/* Logitech Unifying receiver, interface 1, [#17] and [#150] - two units with different
   firmware that put the same fields in the same places. Eight bytes on report ID 2: a
   16-bit button field, then 12-bit X and Y packed low nibble first, the Kensington's
   arrangement with a byte of buttons in front:
     byte 3 = X & 0xFF, byte 4 = (X >> 8) | ((Y & 0xF) << 4), byte 5 = Y >> 4
   then 8-bit wheel and pan. */
static const mouse_case_t m_unifying_cases[] = {
    {"move right (X +1)",     {0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 8,     1,     0,    0,    0,      0},
    {"move left  (X -1)",     {0x02, 0x00, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}, 8,    -1,     0,    0,    0,      0},
    {"move down  (Y +1)",     {0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}, 8,     0,     1,    0,    0,      0},
    {"move up    (Y -1)",     {0x02, 0x00, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00}, 8,     0,    -1,    0,    0,      0},
    {"X +2047 (max)",         {0x02, 0x00, 0x00, 0xFF, 0x07, 0x00, 0x00, 0x00}, 8,  2047,     0,    0,    0,      0},
    {"X -2047 (min)",         {0x02, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00}, 8, -2047,     0,    0,    0,      0},
    {"Y +2047 (max)",         {0x02, 0x00, 0x00, 0x00, 0xF0, 0x7F, 0x00, 0x00}, 8,     0,  2047,    0,    0,      0},
    {"Y -2047 (min)",         {0x02, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x00}, 8,     0, -2047,    0,    0,      0},
    {"scroll up",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 8,     0,     0,    1,    0,      0},
    {"scroll down",           {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 8,     0,     0,   -1,    0,      0},
    {"pan right",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,     0,     0,    0,    1,      0},
    {"pan left",              {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 8,     0,     0,    0,   -1,      0},
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,     0,    0,    0,      1},
    {"button 8, still positive", {0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,  0,     0,    0,    0,    128},
    {"button 16 alone",       {0x02, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,     0,    0,    0, -32768},
    {"all 16 buttons held",   {0x02, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,     0,    0,    0,     -1},
    {"everything at once",    {0x02, 0x1F, 0x00, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 8,  2047, -2047,  127, -127,     31},
};

/* 8BitDo Retro, interface 0, [#57]: the Ultra-Link's eight-byte layout on report ID 3
   instead of 1 - five buttons padded to a byte, 16-bit X and Y, wheel, pan. The
   Keychron 2.4 GHz dongle in [#211] sends these exact bytes as well. */
static const mouse_case_t m_bitdo_mouse_cases[] = {
    {"move right (X +20)",    {0x03, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,    20,      0,   0,    0,  0},
    {"move left  (X -20)",    {0x03, 0x00, 0xEC, 0xFF, 0x00, 0x00, 0x00, 0x00}, 8,   -20,      0,   0,    0,  0},
    {"move down  (Y +20)",    {0x03, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00}, 8,     0,     20,   0,    0,  0},
    {"move up    (Y -20)",    {0x03, 0x00, 0x00, 0x00, 0xEC, 0xFF, 0x00, 0x00}, 8,     0,    -20,   0,    0,  0},
    {"scroll up",             {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 8,     0,      0,   1,    0,  0},
    {"pan right",             {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,     0,      0,   0,    1,  0},
    {"all five buttons",      {0x03, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,     0,      0,   0,    0, 31},
    {"everything at once",    {0x03, 0x1F, 0xFF, 0x7F, 0x01, 0x80, 0x7F, 0x81}, 8, 32767, -32767, 127, -127, 31},
};

/* The PS/2-to-USB converter's interface 1, [#26]. Five bytes on report ID 1: five
   buttons padded to a byte, 8-bit X and Y, 8-bit wheel. No AC pan, so pan is 0
   throughout, and the Motion Wakeup feature bit lives in a feature report, not here.
   The padding row is the same on every device here that pads its buttons: the parser
   reads the pad bits as buttons, see the note on that row. */
static const mouse_case_t m_ps2_converter_cases[] = {
    {"move right (X +10)",    {0x01, 0x00, 0x0A, 0x00, 0x00}, 5,   10,    0,  0, 0,  0},
    {"move left  (X -10)",    {0x01, 0x00, 0xF6, 0x00, 0x00}, 5,  -10,    0,  0, 0,  0},
    {"move down  (Y +10)",    {0x01, 0x00, 0x00, 0x0A, 0x00}, 5,    0,   10,  0, 0,  0},
    {"move up    (Y -10)",    {0x01, 0x00, 0x00, 0xF6, 0x00}, 5,    0,  -10,  0, 0,  0},
    {"X +127, Y -128",        {0x01, 0x00, 0x7F, 0x80, 0x00}, 5,  127, -128,  0, 0,  0},
    {"scroll up",             {0x01, 0x00, 0x00, 0x00, 0x01}, 5,    0,    0,  1, 0,  0},
    {"scroll down",           {0x01, 0x00, 0x00, 0x00, 0xFF}, 5,    0,    0, -1, 0,  0},
    {"button 1 (left)",       {0x01, 0x01, 0x00, 0x00, 0x00}, 5,    0,    0,  0, 0,  1},
    {"button 5",              {0x01, 0x10, 0x00, 0x00, 0x00}, 5,    0,    0,  0, 0, 16},
    {"all five buttons",      {0x01, 0x1F, 0x00, 0x00, 0x00}, 5,    0,    0,  0, 0, 31},
    /* -32, not 0: handle_buttons() folds the constant padding after the buttons into
       the button field, so the byte is read whole and sign-extended. A device that set
       its padding bits would be reporting phantom buttons. */
    {"padding bits set, read as buttons", {0x01, 0xE0, 0x00, 0x00, 0x00}, 5, 0, 0, 0, 0, -32},
    {"drag: btn1 + move",     {0x01, 0x01, 0x0A, 0xF6, 0x00}, 5,   10,  -10,  0, 0,  1},
};

/* The unnamed USB mouse from the same report, [#26]. Seven bytes, no report ID: five
   buttons padded to a byte, 12-bit X and Y packed as on the Kensington but starting at
   byte 1, wheel, AC pan, and then a byte on Generic Desktop usage 0x168 that no map row
   claims. The last two rows check that byte goes nowhere. */
static const mouse_case_t m_issue26_cases[] = {
    {"move right (X +1)",     {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     1,     0,    0,    0,  0},
    {"move left  (X -1)",     {0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00}, 7,    -1,     0,    0,    0,  0},
    {"move down  (Y +1)",     {0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00}, 7,     0,     1,    0,    0,  0},
    {"move up    (Y -1)",     {0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00, 0x00}, 7,     0,    -1,    0,    0,  0},
    {"X +2047, Y -2047",      {0x00, 0xFF, 0x17, 0x80, 0x00, 0x00, 0x00}, 7,  2047, -2047,    0,    0,  0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}, 7,     0,     0,    1,    0,  0},
    {"pan left",              {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 7,     0,     0,    0,   -1,  0},
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0,  1},
    {"all five buttons",      {0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0, 31},
    {"trailing usage 0x168 byte ignored", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 7, 0, 0, 0,  0,  0},
    {"everything at once",    {0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81, 0xAA}, 7,  2047, -2047,  127, -127, 31},
};

/* Corsair Scimitar RGB Elite, interface 0, [#45]. Every row up to the scroll is a report
   the reporter captured with usbhid-dump, eleven bytes: [0x01][32 buttons][X 16][Y 16]
   [wheel][pad]. The 32-bit button field is the first in the corpus, and it is why this
   device sits behind HARNESS_FIELD_32 in the table below: get_report_value() computes
   (1u << size) - 1, which is undefined for a size of 32, and UBSan aborts the run on
   either tree. On the RP2040 the shift comes out as 0, the mask as all ones and the
   buttons decode; on x86 without the sanitiser the mask is 0 and every button reads as
   released. The rows are what the descriptor specifies, ready for a tree that handles
   the width. */
static const mouse_case_t m_scimitar_cases[] = {
    {"move right (X +1), captured", {0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 11,  1,  0, 0, 0,    0},
    {"move left  (X -1), captured", {0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00}, 11, -1,  0, 0, 0,    0},
    {"move up    (Y -1), captured", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00}, 11,  0, -1, 0, 0,    0},
    {"move down  (Y +1), captured", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 11,  0,  1, 0, 0,    0},
    {"up-left, captured",           {0x01, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFF, 0xFE, 0xFF, 0x00, 0x00}, 11, -2, -2, 0, 0,    0},
    {"right button + left, captured", {0x01, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00}, 11, -1, 0, 0, 0,   2},
    {"right button + up, captured", {0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00}, 11,  0, -1, 0, 0,    2},
    {"scroll up",                   {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 11,  0,  0, 1, 0,    0},
    {"side key 12 (button 12)",     {0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 11,  0,  0, 0, 0, 2048},
};

/* The same Scimitar as it presented itself to the DeskHop, [#45]: a 58-byte boot mouse.
   Seven bytes, no report ID: five buttons padded to a byte, 8-bit X, Y and wheel, then
   three bytes of padding. No AC pan. */
static const mouse_case_t m_scimitar_boot_cases[] = {
    {"move right (X +20)",    {0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,   20,   0,  0, 0,  0},
    {"move up-left",          {0x00, 0xF6, 0xF6, 0x00, 0x00, 0x00, 0x00}, 7,  -10, -10,  0, 0,  0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 7,    0,   0,  1, 0,  0},
    {"scroll down",           {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, 7,    0,   0, -1, 0,  0},
    {"all five buttons",      {0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,    0,   0,  0, 0, 31},
    {"padding bytes ignored", {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF}, 7,    0,   0,  0, 0,  0},
    {"drag: btn1 + move",     {0x01, 0x0A, 0xF6, 0x00, 0x00, 0x00, 0x00}, 7,   10, -10,  0, 0,  1},
};

/* The unnamed mouse from [#99], where the middle button came out of a DeskHop as buttons
   2, 8 and 9. Five bytes, no report ID: three buttons and five bits of padding, 12-bit X
   and Y packed as on the Kensington, 8-bit wheel. No AC pan. The middle-button row is the
   one the issue is about: it decodes to 4 and nothing else, so whatever produced 8 and 9
   happened after this point. */
static const mouse_case_t m_issue99_cases[] = {
    {"move right (X +1)",     {0x00, 0x01, 0x00, 0x00, 0x00}, 5,     1,     0,  0, 0, 0},
    {"move left  (X -1)",     {0x00, 0xFF, 0x0F, 0x00, 0x00}, 5,    -1,     0,  0, 0, 0},
    {"move down  (Y +1)",     {0x00, 0x00, 0x10, 0x00, 0x00}, 5,     0,     1,  0, 0, 0},
    {"move up    (Y -1)",     {0x00, 0x00, 0xF0, 0xFF, 0x00}, 5,     0,    -1,  0, 0, 0},
    {"X sign bit only (-2048)", {0x00, 0x00, 0x08, 0x00, 0x00}, 5, -2048,     0,  0, 0, 0},
    {"Y +2047 (max)",         {0x00, 0x00, 0xF0, 0x7F, 0x00}, 5,     0,  2047,  0, 0, 0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x00, 0x01}, 5,     0,     0,  1, 0, 0},
    {"scroll down",           {0x00, 0x00, 0x00, 0x00, 0xFF}, 5,     0,     0, -1, 0, 0},
    {"middle button",         {0x04, 0x00, 0x00, 0x00, 0x00}, 5,     0,     0,  0, 0, 4},
    {"all three buttons",     {0x07, 0x00, 0x00, 0x00, 0x00}, 5,     0,     0,  0, 0, 7},
    {"padding bits set, read as buttons", {0xF8, 0x00, 0x00, 0x00, 0x00}, 5, 0, 0, 0, 0, -8},
};

/* A vial-qmk keyboard's mouse collection, [#151], where a layer change made the pointer
   jump to the parking corner. Six bytes on report ID 2: five buttons padded to a byte,
   8-bit X, Y, wheel and pan. The last row is the report the reporter captured on the
   layer change, seven bytes of zeros: it decodes to nothing, which is the whole point -
   an empty movement report is what reached the output side. */
static const mouse_case_t m_vial_qmk_cases[] = {
    {"move right (X +10)",    {0x02, 0x00, 0x0A, 0x00, 0x00, 0x00}, 6,   10,    0,  0,  0,  0},
    {"move up    (Y -10)",    {0x02, 0x00, 0x00, 0xF6, 0x00, 0x00}, 6,    0,  -10,  0,  0,  0},
    {"scroll up",             {0x02, 0x00, 0x00, 0x00, 0x01, 0x00}, 6,    0,    0,  1,  0,  0},
    {"pan left",              {0x02, 0x00, 0x00, 0x00, 0x00, 0xFF}, 6,    0,    0,  0, -1,  0},
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0,  0,  1},
    {"all five buttons",      {0x02, 0x1F, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0,  0, 31},
    {"padding bits set, read as buttons", {0x02, 0xE0, 0x00, 0x00, 0x00, 0x00}, 6, 0, 0, 0, 0, -32},
    {"layer change, as captured", {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0,   0,  0,  0,  0},
};

/* QMK's shared endpoint, the mouse collection, [#17], [#30], [#61]. The vial layout with
   eight buttons instead of five, so the byte can set bit 7 and sign-extend, as the MX518's
   does. */
static const mouse_case_t m_qmk_shared_mouse_cases[] = {
    {"move right (X +10)",    {0x02, 0x00, 0x0A, 0x00, 0x00, 0x00}, 6,   10,    0,  0,  0,    0},
    {"move up    (Y -10)",    {0x02, 0x00, 0x00, 0xF6, 0x00, 0x00}, 6,    0,  -10,  0,  0,    0},
    {"scroll down",           {0x02, 0x00, 0x00, 0x00, 0xFF, 0x00}, 6,    0,    0, -1,  0,    0},
    {"pan right",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}, 6,    0,    0,  0,  1,    0},
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0,  0,    1},
    {"button 8 alone",        {0x02, 0x80, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0,  0, -128},
    {"all eight buttons",     {0x02, 0xFF, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0,  0,   -1},
    {"everything at once",    {0x02, 0x1F, 0x7F, 0x81, 0x02, 0xFE}, 6,  127, -127,  2, -2,   31},
};

/* Apple Mighty Mouse, [#185]. Six bytes, no report ID: four buttons and four bits of
   padding, then X, Y, Z and Wheel declared as one four-count field, then a vendor byte.
   The parser tracks X, Y and Wheel; the Z byte and the vendor byte go nowhere, and the
   rows named for them check that they stay nowhere. */
static const mouse_case_t m_mighty_mouse_cases[] = {
    {"move right (X +10)",    {0x00, 0x0A, 0x00, 0x00, 0x00, 0x00}, 6,   10,    0,  0, 0,  0},
    {"move up    (Y -10)",    {0x00, 0x00, 0xF6, 0x00, 0x00, 0x00}, 6,    0,  -10,  0, 0,  0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 6,    0,    0,  1, 0,  0},
    {"scroll down",           {0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 6,    0,    0, -1, 0,  0},
    {"Z axis goes nowhere",   {0x00, 0x00, 0x00, 0x7F, 0x00, 0x00}, 6,    0,    0,  0, 0,  0},
    {"vendor byte goes nowhere", {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 6, 0,    0,  0, 0,  0},
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0, 0,  1},
    {"all four buttons",      {0x0F, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,    0,    0,  0, 0, 15},
    {"padding bits set, read as buttons", {0xF0, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0, 0, 0, 0, -16},
    {"everything at once",    {0x0F, 0x7F, 0x81, 0x55, 0xFF, 0xAA}, 6,  127, -127, -1, 0, 15},
};

/* Apple Magic Trackpad, the mouse collection, [#207]. Eight bytes on report ID 2: three
   buttons and five bits of padding, 8-bit X and Y, four bytes of padding. No wheel and no
   pan. Behind HARNESS_BOUNDED_USAGES in the table below, because the same interface
   declares a 1387-byte input against one usage and parsing it takes a tree without the
   usages[] bound down before any report can be decoded. */
static const mouse_case_t m_magic_trackpad_cases[] = {
    {"move right (X +10)",    {0x02, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,   10,    0, 0, 0, 0},
    {"move up    (Y -10)",    {0x02, 0x00, 0x00, 0xF6, 0x00, 0x00, 0x00, 0x00}, 8,    0,  -10, 0, 0, 0},
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,    0,    0, 0, 0, 1},
    {"all three buttons",     {0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8,    0,    0, 0, 0, 7},
    {"padding bits set, read as buttons", {0x02, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0, 0, 0, 0, -8},
    {"padding bytes ignored", {0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF}, 8,    0,    0, 0, 0, 0},
    {"drag: btn1 + move",     {0x02, 0x01, 0x0A, 0xF6, 0x00, 0x00, 0x00, 0x00}, 8,   10,  -10, 0, 0, 1},
};

/* Keyboardio Model 100, interface 4, [#216]: the only absolute pointer among the real
   devices. Six bytes, no report ID: eight buttons, 16-bit X and Y over 0..32767, a
   relative 8-bit wheel. The values come out as declared, as signed reads of a field whose
   declared range never goes negative; what the firmware does with an absolute position
   is mouse.c's business, not the decode's. */
static const mouse_case_t m_keyboardio_mouse_cases[] = {
    {"X 100, absolute",       {0x00, 0x64, 0x00, 0x00, 0x00, 0x00}, 6,   100,     0,  0, 0,    0},
    {"Y 200, absolute",       {0x00, 0x00, 0x00, 0xC8, 0x00, 0x00}, 6,     0,   200,  0, 0,    0},
    {"X 32767, top of range", {0x00, 0xFF, 0x7F, 0x00, 0x00, 0x00}, 6, 32767,     0,  0, 0,    0},
    {"Y 32767, top of range", {0x00, 0x00, 0x00, 0xFF, 0x7F, 0x00}, 6,     0, 32767,  0, 0,    0},
    {"origin, nothing held",  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,     0,     0,  0, 0,    0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 6,     0,     0,  1, 0,    0},
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,     0,     0,  0, 0,    1},
    {"button 8 alone",        {0x80, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,     0,     0,  0, 0, -128},
    {"all eight buttons",     {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00}, 6,     0,     0,  0, 0,   -1},
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
    DEV(sculpt_rx_mouse, HID_PROTOCOL_REPORT, m_sculpt_cases),
    /* The September 2026 sweep. Reused tables are byte-for-byte layout matches: the
       Areson and the Roccat put the Ultra-Link's fields in the Ultra-Link's places on
       report ID 1, the TMK board's mouse is the Gameball's five bytes, the 5.01 Bolt's
       interface 1 begins with the corpus Bolt's 133 bytes, and the second Unifying
       receiver differs from the first only in item order. */
    DEV(g502_mouse, HID_PROTOCOL_REPORT, m_g502_cases),
    DEV(unifying_rx_iface1, HID_PROTOCOL_REPORT, m_unifying_cases),
    DEV(unifying_rx_b_iface1, HID_PROTOCOL_REPORT, m_unifying_cases),
    DEV(areson_trackball, HID_PROTOCOL_REPORT, m_ultralink_cases),
    DEV(roccat_kone_mouse, HID_PROTOCOL_REPORT, m_ultralink_cases),
    DEV(bitdo_retro_mouse, HID_PROTOCOL_REPORT, m_bitdo_mouse_cases),
    DEV(ps2_converter_iface1, HID_PROTOCOL_REPORT, m_ps2_converter_cases),
    DEV(issue26_mouse, HID_PROTOCOL_REPORT, m_issue26_cases),
#ifdef HARNESS_FIELD_32
    /* A 32-bit button field: (1u << 32) is undefined and UBSan aborts the run on every
       tree so far. See the table's comment. */
    DEV(scimitar_iface0, HID_PROTOCOL_REPORT, m_scimitar_cases),
#endif
    DEV(scimitar_boot_mouse, HID_PROTOCOL_REPORT, m_scimitar_boot_cases),
    DEV(bolt_rx_v501_iface1, HID_PROTOCOL_REPORT, m_bolt_rx_cases),
    DEV(issue99_mouse, HID_PROTOCOL_REPORT, m_issue99_cases),
    DEV(fc660c_tmk_mouse, HID_PROTOCOL_REPORT, m_gameball_cases),
    DEV(vial_qmk_iface2, HID_PROTOCOL_REPORT, m_vial_qmk_cases),
    DEV(qmk_shared_endpoint, HID_PROTOCOL_REPORT, m_qmk_shared_mouse_cases),
    DEV(mighty_mouse, HID_PROTOCOL_REPORT, m_mighty_mouse_cases),
#ifdef HARNESS_BOUNDED_USAGES
    /* Only on a tree that stops the usage cursor at the end of usages[]. Elsewhere the
       1387-count input on this interface walks the parser out of its own state. */
    DEV(magic_trackpad_mouse, HID_PROTOCOL_REPORT, m_magic_trackpad_cases),
#endif
    DEV(keyboardio_mouse, HID_PROTOCOL_REPORT, m_keyboardio_mouse_cases),
};

#undef DEV

/* The closed form of each gate above, printed as the run's last line. */
static const kept_out_t mouse_kept_out[] = {
#ifndef HARNESS_FIELD_32
    {"scimitar_iface0", "target cannot read a 32-bit field"},
#endif
#ifndef HARNESS_BOUNDED_USAGES
    {"magic_trackpad_mouse", "target does not stop the usage cursor at the end of usages[]"},
#endif
    {NULL, NULL},
};
