#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "serial_comm.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>

#ifndef IHM_SERIAL_DEVICE
#define IHM_SERIAL_DEVICE "/dev/ttyACM*"
#endif

#ifndef IHM_SERIAL_BAUD
#define IHM_SERIAL_BAUD 115200
#endif

static int s_serial_fd = -1;
static char s_serial_device[64] = "";

/* Resolve the first device matching the glob pattern in IHM_SERIAL_DEVICE.
 * Returns true and fills buf when a match is found, false otherwise. */
static bool serial_find_device(char *buf, size_t buflen)
{
    glob_t g;
    if (glob(IHM_SERIAL_DEVICE, GLOB_NOSORT, NULL, &g) != 0 || g.gl_pathc == 0) {
        globfree(&g);
        return false;
    }
    snprintf(buf, buflen, "%s", g.gl_pathv[0]);
    if (g.gl_pathc > 1) {
        fprintf(stderr, "[serial] %zu ttyACM devices found, using %s\n",
                g.gl_pathc, buf);
    }
    globfree(&g);
    return true;
}

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

    if (s_serial_device[0] == '\0') {
        if (!serial_find_device(s_serial_device, sizeof(s_serial_device))) {
            fprintf(stderr, "[serial] no device matching '%s' found\n",
                    IHM_SERIAL_DEVICE);
            return false;
        }
    }

    int fd = open(s_serial_device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "[serial] unable to open %s: %s\n",
                s_serial_device, strerror(errno));
        s_serial_device[0] = '\0';   /* reset so next call re-scans */
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
            s_serial_device, IHM_SERIAL_BAUD);
    return true;
}

void serial_comm_deinit(void)
{
    if (s_serial_fd >= 0) {
        close(s_serial_fd);
        s_serial_fd = -1;
    }
    s_serial_device[0] = '\0';   /* allow re-scan on next init */
}

bool serial_comm_send_dispense_cl(int pump1_cl, int pump2_cl)
{
    if (pump1_cl < 0) {
        pump1_cl = 0;
    }
    if (pump2_cl < 0) {
        pump2_cl = 0;
    }

    if (s_serial_fd < 0 && !serial_comm_init()) {
        return false;
    }

    char command[64];
    int n = snprintf(command, sizeof(command), "DISPENSE:%d:%d\n", pump1_cl, pump2_cl);
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

    // Read response from the device, which should be "STARTED:<pump1>:<pump2>".
    fd_set readfds;
    struct timeval timeout;
    FD_ZERO(&readfds);
    FD_SET(s_serial_fd, &readfds);
    timeout.tv_sec = 2;  // 2 seconds timeout
    timeout.tv_usec = 0;
    char response[64];
    int ret = select(s_serial_fd + 1, &readfds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(s_serial_fd, &readfds)) {
        ssize_t nread = read(s_serial_fd, response, sizeof(response) - 1);
        if (nread > 0) {
            response[nread] = '\0';
            fprintf(stderr, "[serial] response: %s\n", response);
            if (strncmp(response, "STARTED:", 8) == 0) {
                return true;
            } else {
                fprintf(stderr, "[serial] unexpected response: %s\n", response);
                return false;
            }
        } else {
            fprintf(stderr, "[serial] read failed: %s\n", strerror(errno));
            return false;
        }
    }

    return false;
}

bool serial_comm_send_stop(void)
{
    if (s_serial_fd < 0 && !serial_comm_init()) {
        return false;
    }

    const char *cmd = "STOP\n";
    ssize_t written = write(s_serial_fd, cmd, 5);
    if (written != 5) {
        fprintf(stderr, "[serial] STOP write failed: %s\n", strerror(errno));
        return false;
    }
    tcdrain(s_serial_fd);
    fprintf(stderr, "[serial] STOP sent\n");
    return true;
}