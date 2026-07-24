#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	SERIAL_CAL_EVENT_NONE = 0,
	SERIAL_CAL_EVENT_STARTED,
	SERIAL_CAL_EVENT_PROGRESS,
	SERIAL_CAL_EVENT_STOPPED_BY_SENSOR,
	SERIAL_CAL_EVENT_STOPPED,
	SERIAL_CAL_EVENT_ERROR
} serial_cal_event_type_t;

typedef struct {
	serial_cal_event_type_t type;
	float pump1_value;
	float pump2_value;
	char raw_line[96];
} serial_calibration_event_t;

/* Open and configure the serial link to the microcontroller. */
bool serial_comm_init(void);

/* Close the serial link if currently open. */
void serial_comm_deinit(void);

/* Send command DISPENSE:[PUMP1_CL]:[PUMP2_CL] where quantities are in cL. */
bool serial_comm_send_dispense_cl(int pump1_cl, int pump2_cl);

/* Send STOP command to immediately abort an in-progress dispense. */
bool serial_comm_send_stop(void);

/* Send runtime flow constants to the microcontroller.
 * Returns true when the MCU acknowledges with SETFLOW:OK. */
bool serial_comm_set_flow_constants(float pump1_pulses_per_cl,
									float pump2_flow_cl_per_sec);

/* Start calibration run on a single pump (1 or 2) with target quantity in cL.
 * Returns true when MCU acknowledges with CAL:STARTED. */
bool serial_comm_start_calibration(int pump_index, float quantity_cl);

/* Poll calibration messages without blocking.
 * Returns true when one calibration event is available in @p out. */
bool serial_comm_read_calibration_event(serial_calibration_event_t *out);

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