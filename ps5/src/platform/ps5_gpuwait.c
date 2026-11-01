/* OGBox - the graphics driver's short sleeps, awake.
 * Copyright (C) 2026 Press5elect, GPL-3.0-or-later; keep the attribution "OGBox by Press5elect"
 * (NOTICE).
 *
 * RADV's console winsys (radv_ps5_winsys.c, radv_ps5_sync.c) waits for the GPU by polling its
 * marker for 1.5 ms and then sleeping 1 ms between polls (radv_ps5_sleep_us). A short timed
 * sleep on the console does not last what it is asked for: Crux2 measured a tick of about 10 ms
 * (PS5Timing.h), and OGBox's own clock of the guest's 1 ms sleeps found 18.4 to 18.9 ms. So a
 * wait for work that takes longer than 1.5 ms ended up to 18 ms late. Burnout 3 in a race is
 * that: hakuX's occlusion-test reports are written when the GPU is done with the test's
 * submission, the GPU is done in a millisecond or two, the driver's wait wakes 18 ms later,
 * and the guest, held at its read of the report, flips its frame a vblank or two late (the
 * hitch lines: the frame "ready" 15 to 33 ms before the flip, 54 to 14 fps by the end of a race).
 *
 * ps5/tools/link-title.sh wraps radv_ps5_sleep_us for the whole title (--wrap): a sleep
 * of up to POLL_MAX_US waits awake instead, on the processor's clock, giving its core to the
 * other threads on the way (sched_yield), and for 100 us at most: it is only ever the driver's
 * wait for the GPU, and the loop around the call polls the marker each time it returns, so the
 * GPU's end is seen within 100 us. A longer sleep sleeps for what it is over AWAKE_TAIL_US and
 * waits out the rest awake.
 */

#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

uint64_t ps5px_monotonic_ns(void); /* ps5_clock.c */

/* A sleep of up to this is the driver polling for the GPU (it asks for 100 and 1000 us). */
#define POLL_MAX_US 2000u
#define POLL_US 100u
/* A longer one (the driver has none): the last of it awake, more than the longest late wake-up
 * measured on the console (18.9 ms). */
#define AWAKE_TAIL_US 20000u

void __wrap_radv_ps5_sleep_us(unsigned microseconds)
{
    if (microseconds <= POLL_MAX_US && microseconds > POLL_US) {
        microseconds = POLL_US;
    }
    const uint64_t end = ps5px_monotonic_ns() + (uint64_t)microseconds * 1000u;
    if (microseconds > AWAKE_TAIL_US) {
        const uint64_t ns = (uint64_t)(microseconds - AWAKE_TAIL_US) * 1000u;
        const struct timespec ts = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
        nanosleep(&ts, NULL);
    }
    while (ps5px_monotonic_ns() < end) {
        __builtin_ia32_pause();
        sched_yield();
    }
}
