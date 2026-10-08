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

#ifndef IHM_SERIAL_DEVICE
#define IHM_SERIAL_DEVICE "/dev/ttyACM*"
#endif

#ifndef IHM_SERIAL_BAUD
#define IHM_SERIAL_BAUD 115200
#endif

/* Reply timeouts.  The controller emits PROGRESS every 250 ms while a job
 * runs, so prolonged silence means the link or the firmware is gone. */
#define SERIAL_ACK_TIMEOUT_MS      2500L
#define SERIAL_SILENCE_TIMEOUT_MS  5000L
#define SERIAL_JOB_MAX_MS        180000L
#define SERIAL_RECONNECT_GAP_MS     1000L

static int  s_serial_fd = -1;
static char s_serial_device[128] = "";
static char s_last_error[64] = "";
static long s_last_reconnect_attempt_ms = -10000;

/* ── receive line buffer ────────────────────────────────────────────────── */
static char s_rx_buf[512];
static int  s_rx_len = 0;

/* ── RFID tag queue ─────────────────────────────────────────────────────────
 * Tags are produced by serial_comm_poll() and consumed by screen_manager at a
 * different rate, so they are queued rather than overwritten.  A tiny ring is
 * plenty: a human cannot badge faster than the poll interval. */
#define SERIAL_RFID_QUEUE 4
#define SERIAL_RFID_LEN   32
static char     s_rfid_queue[SERIAL_RFID_QUEUE][SERIAL_RFID_LEN];
static unsigned s_rfid_head = 0;   /* next slot to read  */
static unsigned s_rfid_tail = 0;   /* next slot to write */

/* ── flow constants embedded in every DISPENSE command ───────────────────── */
static float s_pump1_pulses_per_cl                    = 540.42f;
static float s_time_flow_cl_per_sec[SERIAL_MAX_PUMPS] = { 2.8f, 2.8f, 2.8f, 2.8f, 2.8f, 2.8f };
static int   s_time_flow_count                        = 1;

/* ── active job ─────────────────────────────────────────────────────────── */
static serial_job_t s_job;
static uint16_t     s_job_seq        = 0;   /* sequence of the in-flight command */
static long         s_job_sent_ms    = 0;
static long         s_job_started_ms = 0;
static long         s_job_last_rx_ms = 0;
static long         s_job_limit_ms   = SERIAL_JOB_MAX_MS;
static uint16_t     s_seq_counter    = 0;

/* ── small helpers ──────────────────────────────────────────────────────── */

static long serial_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static uint16_t next_seq(void)
{
    /* 0 is reserved for unsolicited lines. */
    if (++s_seq_counter == 0) s_seq_counter = 1;
    return s_seq_counter;
}

static void set_error(const char *code)
{
    snprintf(s_last_error, sizeof(s_last_error), "%s", code ? code : "");
}

const char *serial_comm_last_error(void)
{
    return s_last_error;
}

static bool serial_is_disconnect_errno(int e)
{
    return e == EIO || e == ENODEV || e == ENXIO || e == EBADF || e == EPIPE || e == ECONNRESET;
}

static void job_fail(const char *code)
{
    s_job.state = SERIAL_JOB_FAILED;
    snprintf(s_job.error, sizeof(s_job.error), "%s", code ? code : "UNKNOWN");
    set_error(s_job.error);
    s_job_seq = 0;
    fprintf(stderr, "[serial] job failed: %s\n", s_job.error);
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

    /* A job cannot survive the link it was running on. */
    if (s_job.state == SERIAL_JOB_PENDING || s_job.state == SERIAL_JOB_RUNNING) {
        job_fail("LINK_LOST");
    }
}

/* ── device discovery ───────────────────────────────────────────────────── */

/* Pick the first match of @p pattern in sorted order.
 * Sorted, because the previous "take the last entry" relied on glob(3)
 * returning directory order, which is arbitrary: with two boards plugged in
 * the app talked to whichever one the filesystem happened to list last. */
static bool serial_first_match(const char *pattern, char *buf, size_t buflen)
{
    glob_t g;
    if (!pattern || !buf || buflen == 0) return false;

    if (glob(pattern, 0, NULL, &g) != 0 || g.gl_pathc == 0) {
        globfree(&g);
        return false;
    }

    snprintf(buf, buflen, "%s", g.gl_pathv[0]);
    if (g.gl_pathc > 1) {
        fprintf(stderr, "[serial] %zu devices matched %s, using %s\n",
                g.gl_pathc, pattern, buf);
    }
    globfree(&g);
    return true;
}

static bool serial_pattern_has_glob(const char *pattern)
{
    if (!pattern) return false;
    return strchr(pattern, '*') || strchr(pattern, '?') || strchr(pattern, '[');
}

/* Resolve the controller's device node.
 * /dev/serial/by-id is tried first: those names are built from the USB
 * VID/PID/serial, so they identify the board rather than the order it was
 * enumerated in. */
static bool serial_find_device(char *buf, size_t buflen)
{
    static const char *const by_id[] = {
        "/dev/serial/by-id/*Arduino*",
        "/dev/serial/by-id/*arduino*",
        "/dev/serial/by-id/*ACM*",
    };
    for (unsigned i = 0; i < sizeof(by_id) / sizeof(by_id[0]); i++) {
        if (serial_first_match(by_id[i], buf, buflen)) {
            return true;
        }
    }

    if (serial_first_match(IHM_SERIAL_DEVICE, buf, buflen)) {
        return true;
    }

    /* Configured path is fixed (e.g. /dev/ttyACM0) and stale: scan. */
    if (!serial_pattern_has_glob(IHM_SERIAL_DEVICE)) {
        if (serial_first_match("/dev/ttyACM*", buf, buflen)
                || serial_first_match("/dev/ttyUSB*", buf, buflen)) {
            fprintf(stderr, "[serial] fallback from fixed path %s to %s\n",
                    IHM_SERIAL_DEVICE, buf);
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
            set_error("NO_DEVICE");
            return false;
        }
    }

    /* O_NONBLOCK so neither read nor write can ever stall the LVGL thread. */
    int fd = open(s_serial_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[serial] unable to open %s: %s\n",
                s_serial_device, strerror(errno));
        s_serial_device[0] = '\0';   /* reset so next call re-scans */
        set_error("OPEN_FAILED");
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "[serial] tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        set_error("TCGETATTR");
        return false;
    }

    speed_t speed = serial_baud_to_speed(IHM_SERIAL_BAUD);
    if (speed == 0) {
        fprintf(stderr, "[serial] unsupported baud rate: %d\n", IHM_SERIAL_BAUD);
        close(fd);
        set_error("BAUD");
        return false;
    }

    cfmakeraw(&tty);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[serial] tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        set_error("TCSETATTR");
        return false;
    }

    s_serial_fd = fd;
    s_rx_len = 0;
    s_rx_buf[0] = '\0';
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
    s_rx_len = 0;
    s_rx_buf[0] = '\0';
}

static bool serial_ensure_connected(void)
{
    if (s_serial_fd >= 0) {
        return true;
    }

    long now = serial_now_ms();
    if (now - s_last_reconnect_attempt_ms < SERIAL_RECONNECT_GAP_MS) {
        return false;
    }
    s_last_reconnect_attempt_ms = now;

    return serial_comm_init();
}

/* ── raw I/O ────────────────────────────────────────────────────────────── */

/* Drain the fd into the line buffer.  Non-blocking: returns once the kernel
 * has nothing more. */
static void serial_fill_rx(void)
{
    if (s_serial_fd < 0) return;

    for (;;) {
        int space = (int)(sizeof(s_rx_buf) - 1) - s_rx_len;
        if (space <= 0) {
            /* A sender spewing bytes with no newline must not wedge us. */
            fprintf(stderr, "[serial] rx buffer full without a newline, discarding\n");
            s_rx_len = 0;
            s_rx_buf[0] = '\0';
            space = (int)(sizeof(s_rx_buf) - 1);
        }

        ssize_t n = read(s_serial_fd, s_rx_buf + s_rx_len, (size_t)space);
        if (n > 0) {
            s_rx_len += (int)n;
            s_rx_buf[s_rx_len] = '\0';
            continue;
        }

        if (n == 0) {
            serial_mark_disconnected();
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;                       /* nothing more for now */
        }
        if (errno == EINTR) {
            continue;
        }
        if (serial_is_disconnect_errno(errno)) {
            serial_mark_disconnected();
        }
        return;
    }
}

/* Pop one complete line out of the buffer.  Returns false when none is ready. */
static bool serial_take_line(char *out, size_t out_len)
{
    if (!out || out_len == 0) return false;

    char *nl = memchr(s_rx_buf, '\n', (size_t)s_rx_len);
    if (!nl) return false;

    int line_len = (int)(nl - s_rx_buf);
    if (line_len > 0 && s_rx_buf[line_len - 1] == '\r') {
        line_len--;
    }

    size_t copy_len = (size_t)line_len;
    if (copy_len >= out_len) copy_len = out_len - 1;
    memcpy(out, s_rx_buf, copy_len);
    out[copy_len] = '\0';

    char *next = nl + 1;
    int rem = (int)(s_rx_buf + s_rx_len - next);
    if (rem > 0) memmove(s_rx_buf, next, (size_t)rem);
    s_rx_len = rem;
    s_rx_buf[s_rx_len] = '\0';
    return true;
}

static bool serial_write_all(const char *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(s_serial_fd, data + off, len - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* Output buffer full.  Commands are a couple of hundred bytes
             * against a 4 kB tty buffer, so this means the device is wedged. */
            fprintf(stderr, "[serial] write would block, giving up\n");
            return false;
        }
        fprintf(stderr, "[serial] write failed: %s\n", strerror(errno));
        if (serial_is_disconnect_errno(errno)) {
            serial_mark_disconnected();
        }
        return false;
    }
    return true;
}

/* ── line parsing ───────────────────────────────────────────────────────── */

/* Split "@<seq>:<rest>".  Returns the sequence number, or 0 when the line
 * carries no prefix, and points @p rest at the payload either way. */
static uint16_t line_split_seq(const char *line, const char **rest)
{
    *rest = line;
    if (line[0] != '@') return 0;

    char *end = NULL;
    long v = strtol(line + 1, &end, 10);
    if (!end || *end != ':' || v <= 0 || v > 65535) return 0;

    *rest = end + 1;
    return (uint16_t)v;
}

/* Parse a ':'-separated list of floats. */
static int parse_floats(const char *p, float *out, int max_out)
{
    int count = 0;
    while (count < max_out) {
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p) break;
        out[count++] = v;
        p = end;
        if (*p == ':') { p++; continue; }
        break;
    }
    return count;
}

static void rfid_push(const char *uid)
{
    unsigned next = (s_rfid_tail + 1u) % SERIAL_RFID_QUEUE;
    if (next == s_rfid_head) {
        /* Full: drop the oldest so the newest badge still gets through. */
        s_rfid_head = (s_rfid_head + 1u) % SERIAL_RFID_QUEUE;
    }
    snprintf(s_rfid_queue[s_rfid_tail], SERIAL_RFID_LEN, "%s", uid);
    s_rfid_tail = next;
}

bool serial_comm_read_rfid(char *tag_out, size_t tag_len)
{
    if (!tag_out || tag_len == 0) return false;
    if (s_rfid_head == s_rfid_tail) return false;

    snprintf(tag_out, tag_len, "%s", s_rfid_queue[s_rfid_head]);
    s_rfid_head = (s_rfid_head + 1u) % SERIAL_RFID_QUEUE;
    return true;
}

/* Copy a measured-volume list into the job. */
static void job_store_measured(const char *args)
{
    float vals[SERIAL_MAX_PUMPS];
    int n = parse_floats(args, vals, SERIAL_MAX_PUMPS);
    for (int i = 0; i < n && i < SERIAL_MAX_PUMPS; i++) {
        s_job.measured_cl[i] = vals[i];
    }
    if (n > 0) s_job.measured_valid = true;
}

/* Handle one received line.  Everything the firmware can say arrives here, is
 * matched to the active job by sequence number, and is routed exactly once.
 * That single ownership is what makes concurrent RFID polling and job
 * progress safe: there is no second consumer to race with. */
static void serial_handle_line(const char *line)
{
    const char *rest = NULL;
    uint16_t seq = line_split_seq(line, &rest);

    /* ── unsolicited telemetry ── */
    if (seq == 0) {
        if (strncmp(rest, "RFID:", 5) == 0 && rest[5] != '\0') {
            fprintf(stderr, "[serial] rfid tag: %s\n", rest + 5);
            rfid_push(rest + 5);
            return;
        }

        if (strncmp(rest, "PROGRESS:", 9) == 0) {
            if (s_job.state == SERIAL_JOB_RUNNING) {
                float vals[SERIAL_MAX_PUMPS];
                int n = parse_floats(rest + 9, vals, SERIAL_MAX_PUMPS);
                for (int i = 0; i < n && i < SERIAL_MAX_PUMPS; i++) {
                    s_job.progress_cl[i] = vals[i];
                }
            }
            return;
        }

        if (strncmp(rest, "READY", 5) == 0
                || strncmp(rest, "NFC:", 4) == 0
                || strncmp(rest, "RESET_CAUSE:", 12) == 0) {
            fprintf(stderr, "[serial] mcu: %s\n", rest);
            /* The board rebooted under us, so anything in flight is gone. */
            if (s_job.state == SERIAL_JOB_PENDING || s_job.state == SERIAL_JOB_RUNNING) {
                job_fail("MCU_RESET");
            }
            return;
        }

        fprintf(stderr, "[serial] unhandled line: %s\n", line);
        return;
    }

    /* ── replies ── */
    if (seq != s_job_seq || s_job_seq == 0) {
        /* Answer to a command we have already given up on. */
        fprintf(stderr, "[serial] stale reply for seq %u (active %u): %s\n",
                seq, s_job_seq, rest);
        return;
    }

    s_job_last_rx_ms = serial_now_ms();

    if (strncmp(rest, "STARTED:", 8) == 0 || strcmp(rest, "STARTED") == 0) {
        s_job.state = SERIAL_JOB_RUNNING;
        s_job_started_ms = s_job_last_rx_ms;
        fprintf(stderr, "[serial] job %u running\n", seq);
        return;
    }

    if (strncmp(rest, "CAL_STARTED:", 12) == 0) {
        float vals[2];
        int n = parse_floats(rest + 12, vals, 2);
        s_job.state = SERIAL_JOB_RUNNING;
        s_job_started_ms = s_job_last_rx_ms;
        if (n >= 1) s_job.cal_pump_index = (int)vals[0];
        return;
    }

    if (strncmp(rest, "OK:", 3) == 0) {
        job_store_measured(rest + 3);
        s_job.state = SERIAL_JOB_DONE;
        s_job_seq = 0;
        return;
    }

    if (strncmp(rest, "STOPPED:", 8) == 0) {
        job_store_measured(rest + 8);
        s_job.state = SERIAL_JOB_DONE;
        s_job_seq = 0;
        return;
    }

    /* ABORTED:<CODE>:<measured>...  The run had already started, so the
     * measurements are real even though the outcome is a failure. */
    if (strncmp(rest, "ABORTED:", 8) == 0) {
        const char *code = rest + 8;
        const char *colon = strchr(code, ':');

        char code_buf[sizeof(s_job.error)];
        if (colon) {
            size_t n = (size_t)(colon - code);
            if (n >= sizeof(code_buf)) n = sizeof(code_buf) - 1;
            memcpy(code_buf, code, n);
            code_buf[n] = '\0';
            job_store_measured(colon + 1);
        } else {
            snprintf(code_buf, sizeof(code_buf), "%s", code);
        }

        job_fail(code_buf);
        return;
    }

    if (strncmp(rest, "CAL_DONE:", 9) == 0) {
        float vals[3];
        int n = parse_floats(rest + 9, vals, 3);
        if (n >= 1) s_job.cal_pump_index = (int)vals[0];
        if (n >= 2) s_job.cal_constant = vals[1];
        s_job.cal_stopped_by_sensor = (n >= 3) && (vals[2] != 0.0f);
        s_job.state = SERIAL_JOB_DONE;
        s_job_seq = 0;
        return;
    }

    if (strncmp(rest, "ERROR:", 6) == 0) {
        job_fail(rest + 6);
        return;
    }

    fprintf(stderr, "[serial] unexpected reply for seq %u: %s\n", seq, rest);
}

/* ── polling ────────────────────────────────────────────────────────────── */

void serial_comm_poll(void)
{
    if (!serial_ensure_connected()) {
        return;
    }

    serial_fill_rx();

    char line[160];
    while (serial_take_line(line, sizeof(line))) {
        if (line[0] != '\0') {
            serial_handle_line(line);
        }
    }

    /* Reply timeouts.  Without these a lost ack would leave a job PENDING
     * forever and the UI waiting on it. */
    long now = serial_now_ms();

    if (s_job.state == SERIAL_JOB_PENDING
            && now - s_job_sent_ms > SERIAL_ACK_TIMEOUT_MS) {
        job_fail("TIMEOUT");
        return;
    }

    if (s_job.state == SERIAL_JOB_RUNNING) {
        if (now - s_job_last_rx_ms > SERIAL_SILENCE_TIMEOUT_MS) {
            serial_comm_send_stop();
            job_fail("SILENCE");
            return;
        }
        if (now - s_job_started_ms > s_job_limit_ms) {
            serial_comm_send_stop();
            job_fail("TIMEOUT");
            return;
        }
    }
}

/* ── job API ────────────────────────────────────────────────────────────── */

const serial_job_t *serial_comm_job(void)
{
    return &s_job;
}

bool serial_comm_job_active(void)
{
    return s_job.state == SERIAL_JOB_PENDING || s_job.state == SERIAL_JOB_RUNNING;
}

void serial_comm_job_clear(void)
{
    memset(&s_job, 0, sizeof(s_job));
    s_job.state = SERIAL_JOB_IDLE;
    s_job.kind  = SERIAL_JOB_KIND_NONE;
    s_job_seq   = 0;
}

/* Common prologue: refuse overlapping jobs, reset state, flush stale input. */
static bool job_begin(serial_job_kind_t kind, long limit_ms)
{
    if (serial_comm_job_active()) {
        set_error("BUSY");
        return false;
    }

    if (!serial_ensure_connected()) {
        serial_comm_job_clear();
        job_fail("NO_LINK");
        return false;
    }

    serial_comm_job_clear();
    s_job.kind = kind;
    s_job_limit_ms = limit_ms;

    /* Drop anything buffered from a previous run so a stale PROGRESS cannot
     * be read as this job's first line. */
    tcflush(s_serial_fd, TCIFLUSH);
    s_rx_len = 0;
    s_rx_buf[0] = '\0';
    return true;
}

/* Send @p payload as "@<seq>:<payload>\n" and move the job to PENDING. */
static bool job_send(const char *payload)
{
    uint16_t seq = next_seq();

    char command[320];
    int n = snprintf(command, sizeof(command), "@%u:%s\n", seq, payload);
    if (n < 0 || n >= (int)sizeof(command)) {
        job_fail("FORMAT");
        return false;
    }

    fprintf(stderr, "[serial] tx: %s", command);

    if (!serial_write_all(command, (size_t)n)) {
        job_fail("IO:WRITE");
        return false;
    }

    s_job_seq        = seq;
    s_job.state      = SERIAL_JOB_PENDING;
    s_job_sent_ms    = serial_now_ms();
    s_job_last_rx_ms = s_job_sent_ms;
    s_job_started_ms = s_job_sent_ms;
    return true;
}

void serial_comm_set_dispense_constants(float pump1_pulses_per_cl,
                                        const float *time_flow_cl_per_sec,
                                        int time_count)
{
    if (pump1_pulses_per_cl > 0.0f)
        s_pump1_pulses_per_cl = pump1_pulses_per_cl;
    if (time_count < 0) time_count = 0;
    if (time_count > SERIAL_MAX_PUMPS - 1) time_count = SERIAL_MAX_PUMPS - 1;
    for (int i = 0; i < time_count; i++) {
        if (time_flow_cl_per_sec && time_flow_cl_per_sec[i] > 0.0f)
            s_time_flow_cl_per_sec[i] = time_flow_cl_per_sec[i];
    }
    s_time_flow_count = time_count;
}

bool serial_comm_begin_dispense(const int *pump_cl, int count)
{
    if (!pump_cl || count < 1) {
        set_error("FORMAT");
        return false;
    }
    if (count > SERIAL_MAX_PUMPS) count = SERIAL_MAX_PUMPS;

    if (!job_begin(SERIAL_JOB_KIND_DISPENSE, SERIAL_JOB_MAX_MS)) return false;

    s_job.pump_count = count;

    char payload[256];
    int off = snprintf(payload, sizeof(payload), "DISPENSE");
    if (off < 0) { job_fail("FORMAT"); return false; }

    for (int i = 0; i < count; i++) {
        int v = pump_cl[i] < 0 ? 0 : pump_cl[i];
        s_job.requested_cl[i] = v;

        float flow = (i == 0) ? s_pump1_pulses_per_cl
                              : ((i - 1 < s_time_flow_count)
                                     ? s_time_flow_cl_per_sec[i - 1]
                                     : 2.8f);
        int w = snprintf(payload + off, sizeof(payload) - off, ":%d:%.3f", v, flow);
        if (w < 0 || w >= (int)sizeof(payload) - off) {
            job_fail("FORMAT");
            return false;
        }
        off += w;
    }

    return job_send(payload);
}

bool serial_comm_begin_calibration(int pump_index, float quantity_cl)
{
    if (pump_index < 1 || pump_index > SERIAL_MAX_PUMPS || quantity_cl <= 0.0f) {
        set_error("INVALID_CALIBRATION");
        return false;
    }

    if (!job_begin(SERIAL_JOB_KIND_CALIBRATION, SERIAL_JOB_MAX_MS)) return false;

    s_job.pump_count     = 1;
    s_job.cal_pump_index = pump_index;

    char payload[64];
    if (snprintf(payload, sizeof(payload), "CAL:%d:%.3f", pump_index, quantity_cl)
            >= (int)sizeof(payload)) {
        job_fail("FORMAT");
        return false;
    }

    return job_send(payload);
}

bool serial_comm_begin_purge(int pump_index, uint32_t duration_ms)
{
    if (pump_index < 0 || pump_index > SERIAL_MAX_PUMPS) {
        set_error("INVALID_PURGE");
        return false;
    }

    /* The firmware clamps this too; the pumps must stop even if this process
     * dies or the cable is pulled before the stop command goes out. */
    if (!job_begin(SERIAL_JOB_KIND_PURGE, (long)duration_ms + 10000L)) return false;

    s_job.pump_count = (pump_index == 0) ? SERIAL_MAX_PUMPS : 1;

    char payload[64];
    if (snprintf(payload, sizeof(payload), "PURGE:%d:%u", pump_index,
                 (unsigned)duration_ms) >= (int)sizeof(payload)) {
        job_fail("FORMAT");
        return false;
    }

    return job_send(payload);
}

bool serial_comm_send_stop(void)
{
    if (s_serial_fd < 0) {
        /* Nothing to stop it through; do not try to reconnect just to say so. */
        return false;
    }

    /* STOP is deliberately unsequenced: it must be obeyed whatever the
     * controller thinks is running, including a job this host has given up on. */
    const char *cmd = "@0:STOP\n";
    if (!serial_write_all(cmd, strlen(cmd))) {
        return false;
    }

    fprintf(stderr, "[serial] STOP sent\n");
    return true;
}
