#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open and configure the serial link to the microcontroller. */
bool serial_comm_init(void);

/* Close the serial link if currently open. */
void serial_comm_deinit(void);

/* Send command DISPENSE:[PUMP1_CL]:[PUMP2_CL] where quantities are in cL. */
bool serial_comm_send_dispense_cl(int pump1_cl, int pump2_cl);

/* Send STOP command to immediately abort an in-progress dispense. */
bool serial_comm_send_stop(void);

/* Get last command response/status captured by serial_comm.
 * Examples: STARTED:..., ERROR:NO_OBJECT, ERROR:TIMEOUT, IO:WRITE, TIMEOUT */
const char *serial_comm_last_response(void);

/* Non-blocking RFID tag poll.
 * Returns true and fills tag_out with a NUL-terminated uppercase hex UID
 * when the Arduino has sent a complete "RFID:<uid>\n" line.
 * Returns false when no RFID line is available. */
bool serial_comm_read_rfid(char *tag_out, size_t tag_len);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_COMM_H */