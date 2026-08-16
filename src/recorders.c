/* Stand-ins for the send path, and for the global they read.
 *
 * process_consumer_report() and process_system_report() are lifted verbatim by
 * tools/lift.py, so everything they reach for has to exist here. They end by
 * handing a payload to one of three functions:
 *
 *     CURRENT_BOARD_IS_ACTIVE_OUTPUT ? send_consumer_control(...)  : queue_packet(...)
 *     CURRENT_BOARD_IS_ACTIVE_OUTPUT ? send_system_control(...)    : queue_packet(...)
 *
 * The real send_consumer_control and send_system_control live in keyboard.c one
 * level below the receivers, and they are NOT lifted: they reach queue_cc_packet,
 * queue_system_packet, time_us_64() and state->last_activity[BOARD_ROLE], which is
 * Pico SDK and queue machinery. The cut is here, above them, and it is the right
 * place - what a test wants to know is what the receiver decided to send, which is
 * exactly what these three are handed.
 *
 * queue_packet is the one that carries a length and a packet type; the two send_*
 * functions do not, so len is recorded as -1 for them. That is not a gap: the
 * length is fixed by the constant the caller would have used either way, and the
 * test asserts the payload bytes.
 */
#include "main.h"

sent_t harness_sent;

/* Zeroing via rather than memsetting the struct keeps payload[] readable after a
   case that sent nothing, which is what a failure message wants to print. */
void harness_sent_reset(void) {
    memset(&harness_sent, 0, sizeof(harness_sent));
    harness_sent.len         = -1;
    harness_sent.packet_type = -1;
}

static void record(sent_via_e via, const uint8_t *data, int len, int packet_type) {
    harness_sent.calls++;
    harness_sent.via         = via;
    harness_sent.len         = len;
    harness_sent.packet_type = packet_type;

    /* Copy what the sender would actually transmit. A payload longer than the
       buffer is a harness bug, not a firmware one, so clamp and let the caller
       notice via len rather than overrunning here - this file is linked into a
       test whose whole job is catching overruns. */
    int n = len < 0 ? (int)sizeof(harness_sent.payload) : len;
    if (n > (int)sizeof(harness_sent.payload))
        n = (int)sizeof(harness_sent.payload);

    memcpy(harness_sent.payload, data, (size_t)n);
}

/* CONSUMER_CONTROL_LENGTH and SYSTEM_CONTROL_LENGTH are what the real ones pass on
   to queue_cc_packet / queue_system_packet, so record that much of the payload. */
void send_consumer_control(uint8_t *raw_report, device_t *state) {
    (void)state;
    record(SENT_CONSUMER_LOCAL, raw_report, CONSUMER_CONTROL_LENGTH, -1);
}

void send_system_control(uint8_t *raw_report, device_t *state) {
    (void)state;
    record(SENT_SYSTEM_LOCAL, raw_report, SYSTEM_CONTROL_LENGTH, -1);
}

void queue_packet(const uint8_t *data, enum packet_type_e packet_type, int length) {
    record(SENT_QUEUED, data, length, (int)packet_type);
}

/* The receivers take &global_state and hand it straight to the senders above,
   which ignore it. What actually matters is that CURRENT_BOARD_IS_ACTIVE_OUTPUT
   reads active_output and board_role out of it, which is how a test picks the
   local or the remote path. */
device_t global_state;
