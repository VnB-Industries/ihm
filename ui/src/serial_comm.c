#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "serial_comm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef IHM_SERIAL_DEVICE
#define IHM_SERIAL_DEVICE "/dev/ttyACM0"
#endif

#ifndef IHM_SERIAL_BAUD
#define IHM_SERIAL_BAUD 115200
#endif

static int s_serial_fd = -1;

static speed_t serial_baud_to_speed(int baud)
{
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return (speed_t)0;
    }
}

bool serial_comm_init(void)
{
    if (s_serial_fd >= 0) {
        return true;
    }

    int fd = open(IHM_SERIAL_DEVICE, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[serial] unable to open %s: %s\n",
                IHM_SERIAL_DEVICE, strerror(errno));
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "[serial] tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    speed_t speed = serial_baud_to_speed(IHM_SERIAL_BAUD);
    if (speed == 0) {
        fprintf(stderr, "[serial] unsupported baud rate: %d\n", IHM_SERIAL_BAUD);
        close(fd);
        return false;
    }

    cfmakeraw(&tty);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[serial] tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    s_serial_fd = fd;
    fprintf(stderr, "[serial] connected on %s @ %d baud\n",
            IHM_SERIAL_DEVICE, IHM_SERIAL_BAUD);
    return true;
}

void serial_comm_deinit(void)
{
    if (s_serial_fd >= 0) {
        close(s_serial_fd);
        s_serial_fd = -1;
    }
}

bool serial_comm_send_dispense_cl(int quantity_cl)
{
    if (quantity_cl < 0) {
        quantity_cl = 0;
    }

    if (s_serial_fd < 0 && !serial_comm_init()) {
        return false;
    }

    char command[64];
    int n = snprintf(command, sizeof(command), "DISPENSE:%d\n", quantity_cl);
    if (n < 0 || n >= (int)sizeof(command)) {
        return false;
    }

    ssize_t written = write(s_serial_fd, command, (size_t)n);
    if (written != n) {
        fprintf(stderr, "[serial] write failed: %s\n", strerror(errno));
        return false;
    }

    if (tcdrain(s_serial_fd) != 0) {
        fprintf(stderr, "[serial] tcdrain failed: %s\n", strerror(errno));
        return false;
    }

    return true;
}