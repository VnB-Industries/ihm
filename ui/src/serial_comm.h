#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum pumps supported over the wire (must match firmware MAX_PUMPS). */
#define SERIAL_MAX_PUMPS 6

/* ── link protocol ──────────────────────────────────────────────────────────
 *
 * Every command carries a sequence number and every reply that answers one
 * echoes it:
 *
 *   host -> mcu   @<seq>:DISPENSE:<cl>:<flow>:<cl>:<flow>...
 *                 @<seq>:CAL:<pump>:<quantity_cl>
 *                 @<seq>:PURGE:<pump|0=all>:<duration_ms>
 *                 @<seq>:STOP
 *
 *   mcu  -> host  @<seq>:STARTED:<cl>...          command accepted, running
 *                 @<seq>:OK:<measured_cl>...      finished normally
 *                 @<seq>:STOPPED:<measured_cl>... aborted by STOP
 *                 @<seq>:ABORTED:<CODE>:<measured_cl>...
 *                                                 cut short after starting
 *                 @<seq>:ERROR:<CODE>[:detail]    rejected before starting
 *                 @<seq>:CAL_STARTED:<pump>:<target_cl>
 *                 @<seq>:CAL_DONE:<pump>:<constant>:<by_sensor 0|1>
 *
 * Unsolicited lines carry no @<seq> prefix:
 *
 *                 PROGRESS:<cl>...   RFID:<uid>   READY
 *                 NFC:<state>        RESET_CAUSE:<cause>:<raw>
 *
 * The sequence number is what lets a reply be matched to its command, so a
 * late reply to an abandoned command, or the boot banner emitted when the MCU
 * resets mid-command, is discarded instead of being mistaken for an ack. */

/* ── asynchronous jobs ──────────────────────────────────────────────────────
 *
 * A dispense, a calibration and a purge are all "jobs": one at a time, driven
 * by serial_comm_poll() rather than by blocking the caller.  Nothing in this
 * module waits on the controller, because every caller runs on the LVGL
 * thread and a stalled link used to freeze the whole UI for seconds. */

typedef enum {
    SERIAL_JOB_IDLE = 0,  /* nothing in flight                               */
    SERIAL_JOB_PENDING,   /* command sent, waiting for STARTED or ERROR      */
    SERIAL_JOB_RUNNING,   /* STARTED seen, pumps running                     */
    SERIAL_JOB_DONE,      /* terminal: finished, see measured_cl             */
    SERIAL_JOB_FAILED     /* terminal: rejected or aborted, see error        */
} serial_job_state_t;

typedef enum {
    SERIAL_JOB_KIND_NONE = 0,
    SERIAL_JOB_KIND_DISPENSE,
    SERIAL_JOB_KIND_CALIBRATION,
    SERIAL_JOB_KIND_PURGE
} serial_job_kind_t;

typedef struct {
    serial_job_state_t state;
    serial_job_kind_t  kind;

    int   pump_count;                        /* pumps this job addresses     */
    int   requested_cl[SERIAL_MAX_PUMPS];    /* what was asked for           */
    float progress_cl[SERIAL_MAX_PUMPS];     /* latest PROGRESS, live        */
    float measured_cl[SERIAL_MAX_PUMPS];     /* what was actually delivered  */

    /* True once the controller has reported real measurements, which it does
     * for a completed pour and for one cut short after starting.  A pour the
     * controller refused outright never ran, so it has none: that is the
     * difference between crediting nothing and crediting what came out. */
    bool  measured_valid;

    /* Error code without its prefix, e.g. "NO_OBJECT", "TIMEOUT", "BUSY",
     * "IO:WRITE".  Empty unless state is SERIAL_JOB_FAILED.  A FAILED job may
     * still carry measurements - see measured_valid. */
    char  error[48];

    /* Calibration results, valid when kind is SERIAL_JOB_KIND_CALIBRATION
     * and state is SERIAL_JOB_DONE. */
    int   cal_pump_index;      /* 1-based                                    */
    float cal_constant;        /* pulses/cL for pump 1, else cL/s            */
    bool  cal_stopped_by_sensor;
} serial_job_t;

/* Open and configure the serial link to the microcontroller. */
bool serial_comm_init(void);

/* Close the serial link if currently open. */
void serial_comm_deinit(void);

/* Read everything that arrived, advance the active job, queue RFID tags and
 * enforce the per-state reply timeouts.  Never blocks.  Must be called
 * regularly (every few tens of ms) for any job to make progress. */
void serial_comm_poll(void);

/* The current job.  Never NULL; state is SERIAL_JOB_IDLE when none is active. */
const serial_job_t *serial_comm_job(void);

/* Acknowledge a terminal job (DONE or FAILED) and return to IDLE.
 * A terminal job is latched until this is called, so a caller cannot miss a
 * completion that happened between two of its own timer ticks. */
void serial_comm_job_clear(void);

/* True when a job occupies the link (PENDING or RUNNING). */
bool serial_comm_job_active(void);

/* Start a dispense of @p count per-pump volumes (index order, cL).
 * Returns false when the link is down or a job is already active; otherwise
 * the job moves to PENDING and the outcome arrives via serial_comm_job(). */
bool serial_comm_begin_dispense(const int *pump_cl, int count);

/* Start a calibration run on one pump (1..N) with a target quantity in cL. */
bool serial_comm_begin_calibration(int pump_index, float quantity_cl);

/* Start a purge.  @p pump_index is 1..N for a single pump or 0 for all.
 * @p duration_ms is bounded by the firmware as well, so the pumps stop even
 * if this process dies or the cable is pulled mid-purge. */
bool serial_comm_begin_purge(int pump_index, uint32_t duration_ms);

/* Ask the controller to abort whatever is running, right now. */
bool serial_comm_send_stop(void);

/* Store flow constants used when building DISPENSE commands; no serial exchange.
 * Call at startup and after any calibration update. */
void serial_comm_set_dispense_constants(float pump1_pulses_per_cl,
                                        const float *time_flow_cl_per_sec,
                                        int time_count);

/* Last link-level or job-level error text, for status lines.  Never NULL. */
const char *serial_comm_last_error(void);

/* Non-blocking RFID tag poll, fed by serial_comm_poll().
 * Returns true and fills tag_out with a NUL-terminated uppercase hex UID when
 * a tag has been scanned since the last call. */
bool serial_comm_read_rfid(char *tag_out, size_t tag_len);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_COMM_H */
