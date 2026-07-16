#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_COMM_H */