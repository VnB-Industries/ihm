#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "serial_comm.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>

#ifndef IHM_SERIAL_DEVICE
#define IHM_SERIAL_DEVICE "/dev/ttyACM*"
#endif

#ifndef IHM_SERIAL_BAUD
#define IHM_SERIAL_BAUD 9600
#endif

static int s_serial_fd = -1;
static char s_serial_device[64] = "";
static char s_last_response[96] = "";
static long s_last_reconnect_attempt_ms = -10000;

/* ── unsolicited-message line buffer (RFID readings) ─────────────────── */
static char s_rx_buf[256];
static int  s_rx_len = 0;

static bool serial_pattern_has_glob(const char *pattern)
{
    if (!pattern) return false;
    return strchr(pattern, '*') || strchr(pattern, '?') || strchr(pattern, '[');
}

static bool serial_find_device_from_pattern(const char *pattern, char *buf, size_t buflen)
{
    glob_t g;
    if (!pattern || !buf || buflen == 0) return false;

    if (glob(pattern, GLOB_NOSORT, NULL, &g) != 0 || g.gl_pathc == 0) {
        globfree(&g);
        return false;
    }

    /* Prefer the last entry; on Linux this is commonly the most recent ACM/USB index. */
    const char *chosen = g.gl_pathv[g.gl_pathc - 1];
    snprintf(buf, buflen, "%s", chosen);
    if (g.gl_pathc > 1) {
        fprintf(stderr, "[serial] %zu devices matched %s, using %s\n",
                g.gl_pathc, pattern, buf);
    }
    globfree(&g);
    return true;
}

static long serial_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void serial_mark_disconnected(void)
{
    if (s_serial_fd >= 0) {
        close(s_serial_fd);
        s_serial_fd = -1;
    }
    s_serial_device[0] = '\0';
    s_rx_len = 0;
    s_rx_buf[0] = '\0';
}

static bool serial_is_disconnect_errno(int e)
{
    return e == EIO || e == ENODEV || e == ENXIO || e == EBADF || e == EPIPE || e == ECONNRESET;
}

static bool serial_ensure_connected(void)
{
    if (s_serial_fd >= 0) {
        return true;
    }

    long now = serial_now_ms();
    if (now - s_last_reconnect_attempt_ms < 1000L) {
        return false;
    }
    s_last_reconnect_attempt_ms = now;

    return serial_comm_init();
}

static bool serial_parse_floats(const char *line, const char *prefix,
                                float *out, int max_out, int *count_out)
{
    if (!line || !prefix || !out || max_out <= 0 || !count_out) {
        return false;
    }

    size_t n = strlen(prefix);
    if (strncmp(line, prefix, n) != 0) {
        return false;
    }

    const char *p = line + n;
    int count = 0;
    while (count < max_out) {
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p) {
            break;
        }
        out[count++] = v;
        p = end;
        if (*p == ':') {
            p++;
            continue;
        }
        break;
    }

    if (count == 0) {
        return false;
    }
    *count_out = count;
    return true;
}

/* Read one newline-terminated line without blocking.
 * Returns true when a full line is available. */
static bool serial_read_next_line_nonblocking(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return false;
    }

    out[0] = '\0';
    if (!serial_ensure_connected()) {
        return false;
    }

    char *nl = memchr(s_rx_buf, '\n', (size_t)s_rx_len);
    if (!nl) {
        fd_set readfds;
        struct timeval tv = {0, 0};
        FD_ZERO(&readfds);
        FD_SET(s_serial_fd, &readfds);

        int sret = select(s_serial_fd + 1, &readfds, NULL, NULL, &tv);
        if (sret < 0) {
            if (serial_is_disconnect_errno(errno)) {
                serial_mark_disconnected();
            }
            return false;
        }

        if (sret == 0) {
            return false;
        }

        ssize_t n = read(s_serial_fd,
                         s_rx_buf + s_rx_len,
                         (sizeof(s_rx_buf) - 1) - (size_t)s_rx_len);
        if (n <= 0) {
            if (n == 0 || serial_is_disconnect_errno(errno)) {
                serial_mark_disconnected();
            }
            return false;
        }

        s_rx_len += (int)n;
        s_rx_buf[s_rx_len] = '\0';
        nl = memchr(s_rx_buf, '\n', (size_t)s_rx_len);
    }

    if (!nl) {
        if (s_rx_len >= (int)(sizeof(s_rx_buf) - 1)) {
            s_rx_len = 0;
            s_rx_buf[0] = '\0';
        }
        return false;
    }

    int line_len = (int)(nl - s_rx_buf);
    if (line_len > 0 && s_rx_buf[line_len - 1] == '\r') {
        line_len--;
    }
    if (line_len < 0) {
        line_len = 0;
    }

    size_t copy_len = (size_t)line_len;
    if (copy_len >= out_len) {
        copy_len = out_len - 1;
    }
    memcpy(out, s_rx_buf, copy_len);
    out[copy_len] = '\0';

    char *next = nl + 1;
    int rem = (int)(s_rx_buf + s_rx_len - next);
    if (rem > 0) {
        memmove(s_rx_buf, next, (size_t)rem);
    }
    s_rx_len = rem;
    s_rx_buf[s_rx_len] = '\0';
    return true;
}

/* Read one newline-terminated line with timeout into out.
 * Returns true when a full line is read, false on timeout/error. */
static bool serial_read_line_with_timeout(char *out, size_t out_len, int timeout_ms)
{
    if (!out || out_len == 0 || s_serial_fd < 0 || timeout_ms < 0) {
        return false;
    }

    size_t len = 0;
    out[0] = '\0';

    struct timespec start_ts;
    if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
        return false;
    }

    while (1) {
        struct timespec now_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) {
            return false;
        }

        long elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000L
                        + (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000L;
        long remaining_ms = (long)timeout_ms - elapsed_ms;
        if (remaining_ms <= 0) {
            return false;
        }

        fd_set readfds;
        struct timeval tv;
        tv.tv_sec = (time_t)(remaining_ms / 1000L);
        tv.tv_usec = (suseconds_t)((remaining_ms % 1000L) * 1000L);

        FD_ZERO(&readfds);
        FD_SET(s_serial_fd, &readfds);

        int ret = select(s_serial_fd + 1, &readfds, NULL, NULL, &tv);
        if (ret <= 0) {
            return false;
        }

        char c;
        ssize_t n = read(s_serial_fd, &c, 1);
        if (n <= 0) {
            return false;
        }

        if (c == '\n') {
            break;
        }

        if (c == '\r') {
            continue;
        }

        if (len + 1 < out_len) {
            out[len++] = c;
            out[len] = '\0';
        }
    }

    return len > 0;
}

/* Resolve the first device matching the glob pattern in IHM_SERIAL_DEVICE.
 * Returns true and fills buf when a match is found, false otherwise. */
static bool serial_find_device(char *buf, size_t buflen)
{
    if (serial_find_device_from_pattern(IHM_SERIAL_DEVICE, buf, buflen)) {
        return true;
    }

    /* If configured path is fixed (e.g. /dev/ttyACM0) and stale, fallback to dynamic scan. */
    if (!serial_pattern_has_glob(IHM_SERIAL_DEVICE)) {
        if (serial_find_device_from_pattern("/dev/ttyACM*", buf, buflen)) {
            fprintf(stderr, "[serial] fallback from fixed path %s to %s\n", IHM_SERIAL_DEVICE, buf);
            return true;
        }
        if (serial_find_device_from_pattern("/dev/ttyUSB*", buf, buflen)) {
            fprintf(stderr, "[serial] fallback from fixed path %s to %s\n", IHM_SERIAL_DEVICE, buf);
            return true;
        }
    }

    return false;
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

const char *serial_comm_last_response(void)
{
    return s_last_response;
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
    int vols[2] = { pump1_cl, pump2_cl };
    return serial_comm_send_dispense_cl_n(vols, 2);
}

bool serial_comm_send_dispense_cl_n(const int *pump_cl, int count)
{
    s_last_response[0] = '\0';

    if (!pump_cl || count < 1) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
        return false;
    }
    if (count > SERIAL_MAX_PUMPS) {
        count = SERIAL_MAX_PUMPS;
    }

    if (s_serial_fd < 0 && !serial_comm_init()) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:INIT");
        return false;
    }

    /* Discard any unread bytes (stale PROGRESS / OK / ERROR replies from
     * previous dispenses) before issuing a new command.  If this data is left
     * in the kernel receive buffer, it eventually fills up and Arduino's
     * Serial.print() blocks, stalling loop() and causing 2-5 s dispensing lag
     * after several uses. */
    tcflush(s_serial_fd, TCIFLUSH);
    s_rx_len = 0;   /* also discard any partially buffered line */

    char command[128];
    int off = snprintf(command, sizeof(command), "DISPENSE");
    for (int i = 0; i < count; i++) {
        int v = pump_cl[i] < 0 ? 0 : pump_cl[i];
        int w = snprintf(command + off, sizeof(command) - off, ":%d", v);
        if (w < 0 || w >= (int)sizeof(command) - off) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
            return false;
        }
        off += w;
    }
    int w = snprintf(command + off, sizeof(command) - off, "\n");
    if (w < 0 || w >= (int)sizeof(command) - off) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
        return false;
    }
    off += w;
    int n = off;

    for (int attempt = 0; attempt < 2; attempt++) {
        const long ack_timeout_ms = (attempt == 0) ? 2000L : 3500L;

        ssize_t written = write(s_serial_fd, command, (size_t)n);
        if (written != n) {
            fprintf(stderr, "[serial] write failed: %s\n", strerror(errno));
            if (serial_is_disconnect_errno(errno)) {
                serial_mark_disconnected();
                if (attempt == 0 && serial_ensure_connected()) {
                    continue;
                }
            }
            snprintf(s_last_response, sizeof(s_last_response), "IO:WRITE");
            return false;
        }

        if (tcdrain(s_serial_fd) != 0) {
            fprintf(stderr, "[serial] tcdrain failed: %s\n", strerror(errno));
            if (serial_is_disconnect_errno(errno)) {
                serial_mark_disconnected();
            }
            snprintf(s_last_response, sizeof(s_last_response), "IO:DRAIN");
            return false;
        }

        // Wait for a valid ack line. Ignore unsolicited status/noise lines.
        struct timespec start_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
            return false;
        }

        bool saw_boot_banner = false;

        while (1) {
            struct timespec now_ts;
            if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) {
                snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
                return false;
            }

            long elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000L
                            + (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000L;
            long remaining_ms = ack_timeout_ms - elapsed_ms;
            if (remaining_ms <= 0) {
                if (saw_boot_banner && attempt == 0) {
                    fprintf(stderr, "[serial] command likely lost during MCU reset, waiting then retrying once\n");
                    tcflush(s_serial_fd, TCIFLUSH);
                    usleep(1200000);
                    break;
                }
                fprintf(stderr, "[serial] timeout waiting for dispense response\n");
                snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
                return false;
            }

            char response[96];
            if (!serial_read_line_with_timeout(response, sizeof(response), (int)remaining_ms)) {
                if (saw_boot_banner && attempt == 0) {
                    fprintf(stderr, "[serial] command likely lost during MCU reset, waiting then retrying once\n");
                    tcflush(s_serial_fd, TCIFLUSH);
                    usleep(1200000);
                    break;
                }
                fprintf(stderr, "[serial] timeout waiting for dispense response\n");
                snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
                return false;
            }

            // If the line contains a known token after leading noise, re-anchor to it.
            char *started = strstr(response, "STARTED:");
            char *error = strstr(response, "ERROR:");
            char *rfid = strstr(response, "RFID:");
            char *ready = strstr(response, "READY");
            char *nfc = strstr(response, "NFC:NO_SHIELD");
            char *resetCause = strstr(response, "RESET_CAUSE:");
            char *progress = strstr(response, "PROGRESS:");
            char *ok = strstr(response, "OK:");
            char *stopped = strstr(response, "STOPPED:");
            char *calStarted = strstr(response, "CAL:STARTED");
            char *calStopped = strstr(response, "CAL:STOPPED");
            char *calStoppedBySensor = strstr(response, "CAL:STOPPED_BY_SENSOR:");
            char *setflowOk = strstr(response, "SETFLOW:OK:");

            const char *line = response;
            if (started) line = started;
            else if (error) line = error;
            else if (rfid) line = rfid;
            else if (resetCause) line = resetCause;
            else if (nfc) line = nfc;
            else if (ready) line = ready;
            else if (progress) line = progress;
            else if (ok) line = ok;
            else if (stopped) line = stopped;
            else if (calStarted) line = calStarted;
            else if (calStoppedBySensor) line = calStoppedBySensor;
            else if (calStopped) line = calStopped;
            else if (setflowOk) line = setflowOk;

            fprintf(stderr, "[serial] response: %s\n", line);

            if (strncmp(line, "STARTED:", 8) == 0) {
                snprintf(s_last_response, sizeof(s_last_response), "%s", line);
                return true;
            }

            if (strncmp(line, "ERROR:", 6) == 0) {
                snprintf(s_last_response, sizeof(s_last_response), "%s", line);
                fprintf(stderr, "[serial] dispense rejected: %s\n", line);
                return false;
            }

            if (rfid || ready || nfc || resetCause || progress || ok || stopped
                    || calStarted || calStoppedBySensor || calStopped || setflowOk) {
                if (ready || nfc || resetCause)
                    saw_boot_banner = true;
                fprintf(stderr, "[serial] ignoring unsolicited line: %s\n", line);
                continue;
            }

            fprintf(stderr, "[serial] unexpected response: %s\n", line);
            // Keep waiting: spurious noise should not fail command immediately.
        }
    }

    fprintf(stderr, "[serial] timeout waiting for dispense response\n");
    snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
    return false;
}

bool serial_comm_send_stop(void)
{
    if (!serial_ensure_connected()) {
        return false;
    }

    const char *cmd = "STOP\n";
    ssize_t written = write(s_serial_fd, cmd, 5);
    if (written != 5) {
        fprintf(stderr, "[serial] STOP write failed: %s\n", strerror(errno));
        if (serial_is_disconnect_errno(errno)) {
            serial_mark_disconnected();
        }
        return false;
    }
    if (tcdrain(s_serial_fd) != 0 && serial_is_disconnect_errno(errno)) {
        serial_mark_disconnected();
    }
    fprintf(stderr, "[serial] STOP sent\n");
    return true;
}

bool serial_comm_set_flow_constants(float pump1_pulses_per_cl,
                                    const float *time_flow_cl_per_sec,
                                    int time_count)
{
    s_last_response[0] = '\0';

    if (pump1_pulses_per_cl <= 0.0f) {
        snprintf(s_last_response, sizeof(s_last_response), "ERROR:INVALID_CONSTANTS");
        return false;
    }
    if (time_count < 0) {
        time_count = 0;
    }
    if (time_count > SERIAL_MAX_PUMPS - 1) {
        time_count = SERIAL_MAX_PUMPS - 1;
    }
    for (int i = 0; i < time_count; i++) {
        if (!time_flow_cl_per_sec || time_flow_cl_per_sec[i] <= 0.0f) {
            snprintf(s_last_response, sizeof(s_last_response), "ERROR:INVALID_CONSTANTS");
            return false;
        }
    }

    if (!serial_ensure_connected()) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:INIT");
        return false;
    }

    tcflush(s_serial_fd, TCIFLUSH);
    s_rx_len = 0;
    s_rx_buf[0] = '\0';

    char command[128];
    int off = snprintf(command, sizeof(command), "SETFLOW:%.3f", pump1_pulses_per_cl);
    for (int i = 0; i < time_count; i++) {
        int w = snprintf(command + off, sizeof(command) - off, ":%.3f",
                         time_flow_cl_per_sec[i]);
        if (w < 0 || w >= (int)sizeof(command) - off) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
            return false;
        }
        off += w;
    }
    int w = snprintf(command + off, sizeof(command) - off, "\n");
    if (w < 0 || w >= (int)sizeof(command) - off) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
        return false;
    }
    off += w;
    int n = off;

    for (int attempt = 0; attempt < 2; attempt++) {
        const long ack_timeout_ms = (attempt == 0) ? 2000L : 3500L;
        bool saw_boot_banner = false;

        if (write(s_serial_fd, command, (size_t)n) != n) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:WRITE");
            return false;
        }

        if (tcdrain(s_serial_fd) != 0) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:DRAIN");
            return false;
        }

        struct timespec start_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
            return false;
        }

        while (1) {
            struct timespec now_ts;
            if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) {
                snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
                return false;
            }

            long elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000L
                            + (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000L;
            long remaining_ms = ack_timeout_ms - elapsed_ms;
            if (remaining_ms <= 0) {
                if (saw_boot_banner && attempt == 0) {
                    tcflush(s_serial_fd, TCIFLUSH);
                    usleep(1200000);
                    break;
                }
                snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
                return false;
            }

            char response[96];
            if (!serial_read_line_with_timeout(response, sizeof(response), (int)remaining_ms)) {
                if (saw_boot_banner && attempt == 0) {
                    tcflush(s_serial_fd, TCIFLUSH);
                    usleep(1200000);
                    break;
                }
                snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
                return false;
            }

            char *ok = strstr(response, "SETFLOW:OK:");
            char *err = strstr(response, "ERROR:");
            char *ready = strstr(response, "READY");
            char *resetCause = strstr(response, "RESET_CAUSE:");
            char *nfc = strstr(response, "NFC:");

            const char *line = ok ? ok : (err ? err : response);
            if (ok) {
                snprintf(s_last_response, sizeof(s_last_response), "%s", line);
                return true;
            }
            if (err) {
                snprintf(s_last_response, sizeof(s_last_response), "%s", line);
                return false;
            }
            if (ready || resetCause || nfc) {
                saw_boot_banner = true;
            }
            fprintf(stderr, "[serial] ignoring unsolicited line: %s\n", response);
        }
    }

    snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
    return false;
}

bool serial_comm_start_calibration(int pump_index, float quantity_cl)
{
    s_last_response[0] = '\0';

    if (pump_index < 1 || pump_index > SERIAL_MAX_PUMPS || quantity_cl <= 0.0f) {
        snprintf(s_last_response, sizeof(s_last_response), "ERROR:INVALID_CALIBRATION");
        return false;
    }

    if (!serial_ensure_connected()) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:INIT");
        return false;
    }

    tcflush(s_serial_fd, TCIFLUSH);
    s_rx_len = 0;
    s_rx_buf[0] = '\0';

    char command[64];
    int n = snprintf(command, sizeof(command), "CAL:%d:%.3f\n", pump_index, quantity_cl);
    if (n < 0 || n >= (int)sizeof(command)) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:FORMAT");
        return false;
    }

    if (write(s_serial_fd, command, (size_t)n) != n) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:WRITE");
        return false;
    }

    if (tcdrain(s_serial_fd) != 0) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:DRAIN");
        return false;
    }

    struct timespec start_ts;
    if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
        snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
        return false;
    }

    while (1) {
        struct timespec now_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) {
            snprintf(s_last_response, sizeof(s_last_response), "IO:CLOCK");
            return false;
        }

        long elapsed_ms = (now_ts.tv_sec - start_ts.tv_sec) * 1000L
                        + (now_ts.tv_nsec - start_ts.tv_nsec) / 1000000L;
        long remaining_ms = 3000L - elapsed_ms;
        if (remaining_ms <= 0) {
            snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
            return false;
        }

        char response[96];
        if (!serial_read_line_with_timeout(response, sizeof(response), (int)remaining_ms)) {
            snprintf(s_last_response, sizeof(s_last_response), "TIMEOUT");
            return false;
        }

        char *started = strstr(response, "CAL:STARTED:");
        char *err = strstr(response, "ERROR:");
        char *rfid = strstr(response, "RFID:");
        char *ready = strstr(response, "READY");
        char *nfc = strstr(response, "NFC:");
        char *resetCause = strstr(response, "RESET_CAUSE:");
        char *progress = strstr(response, "PROGRESS:");

        const char *line = started ? started :
                           (err ? err :
                           (rfid ? rfid :
                           (resetCause ? resetCause :
                           (nfc ? nfc :
                           (ready ? ready :
                           (progress ? progress : response))))));

        if (started) {
            snprintf(s_last_response, sizeof(s_last_response), "%s", line);
            return true;
        }

        if (err) {
            snprintf(s_last_response, sizeof(s_last_response), "%s", line);
            return false;
        }

        fprintf(stderr, "[serial] ignoring unsolicited line: %s\n", line);
    }
}

bool serial_comm_read_calibration_event(serial_calibration_event_t *out)
{
    if (!out) {
        return false;
    }

    char line[96];
    while (serial_read_next_line_nonblocking(line, sizeof(line))) {
        float vals[SERIAL_MAX_PUMPS];
        int count = 0;

        /* CAL:STOPPED* carries <pump_index>:<new_flow_constant>. */
        if (serial_parse_floats(line, "CAL:STOPPED_BY_SENSOR:", vals, SERIAL_MAX_PUMPS, &count)) {
            out->type = SERIAL_CAL_EVENT_STOPPED_BY_SENSOR;
            out->pump_index = (count >= 1) ? (int)vals[0] : 0;
            out->value = (count >= 2) ? vals[1] : 0.0f;
            snprintf(out->raw_line, sizeof(out->raw_line), "%s", line);
            return true;
        }

        if (serial_parse_floats(line, "CAL:STOPPED:", vals, SERIAL_MAX_PUMPS, &count)) {
            out->type = SERIAL_CAL_EVENT_STOPPED;
            out->pump_index = (count >= 1) ? (int)vals[0] : 0;
            out->value = (count >= 2) ? vals[1] : 0.0f;
            snprintf(out->raw_line, sizeof(out->raw_line), "%s", line);
            return true;
        }

        /* CAL:STARTED:<pump_index>:<target_cl>. */
        if (serial_parse_floats(line, "CAL:STARTED:", vals, SERIAL_MAX_PUMPS, &count)) {
            out->type = SERIAL_CAL_EVENT_STARTED;
            out->pump_index = (count >= 1) ? (int)vals[0] : 0;
            out->value = (count >= 2) ? vals[1] : 0.0f;
            snprintf(out->raw_line, sizeof(out->raw_line), "%s", line);
            return true;
        }

        /* During calibration PROGRESS carries a single live-volume value. */
        if (serial_parse_floats(line, "PROGRESS:", vals, SERIAL_MAX_PUMPS, &count)) {
            out->type = SERIAL_CAL_EVENT_PROGRESS;
            out->pump_index = 0;
            out->value = (count >= 1) ? vals[0] : 0.0f;
            snprintf(out->raw_line, sizeof(out->raw_line), "%s", line);
            return true;
        }

        if (strncmp(line, "ERROR:", 6) == 0) {
            out->type = SERIAL_CAL_EVENT_ERROR;
            out->pump_index = 0;
            out->value = 0.0f;
            snprintf(out->raw_line, sizeof(out->raw_line), "%s", line);
            snprintf(s_last_response, sizeof(s_last_response), "%s", line);
            return true;
        }

        if (strncmp(line, "RFID:", 5) == 0 || strncmp(line, "READY", 5) == 0
                || strncmp(line, "NFC:", 4) == 0 || strncmp(line, "RESET_CAUSE:", 12) == 0
                || strncmp(line, "STARTED:", 8) == 0 || strncmp(line, "STOPPED:", 8) == 0
                || strncmp(line, "OK:", 3) == 0 || strncmp(line, "SETFLOW:OK", 10) == 0) {
            fprintf(stderr, "[serial] telemetry: %s\n", line);
            continue;
        }

        fprintf(stderr, "[serial] ignoring line: %s\n", line);
    }

    return false;
}

bool serial_comm_read_rfid(char *tag_out, size_t tag_len)
{
    if (!tag_out || tag_len == 0) {
        return false;
    }

    char line[96];
    while (serial_read_next_line_nonblocking(line, sizeof(line))) {
        if (strncmp(line, "RFID:", 5) == 0 && strlen(line) > 5) {
            strncpy(tag_out, line + 5, tag_len - 1);
            tag_out[tag_len - 1] = '\0';
            fprintf(stderr, "[serial] rfid tag: %s\n", tag_out);
            return true;
        }

        fprintf(stderr, "[serial] telemetry: %s\n", line);
    }

    return false;
}