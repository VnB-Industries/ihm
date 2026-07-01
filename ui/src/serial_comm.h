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

/* Send command DISPENSE:[QUANTITY] where QUANTITY is in cL. */
bool serial_comm_send_dispense_cl(int quantity_cl);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_COMM_H */