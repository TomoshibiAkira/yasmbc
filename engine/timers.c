/* timers.c - faithful ownership of the NES timer RAM bank */
#include <string.h>

#include "timers.h"
#include "constants/defs.h"
#include "constants/globals.h"

/* DecTimers uses X=$14 for the ordinary pass and X=$23 when the interval
 * divider expires.  The latter is an intentional visit to $07A3: it is
 * adjacent to the named DemoTimer ($07A2), but it is still part of the 6502
 * indexed loop and must not alias TimerControl or another C global. */
void Timers_DecTimers(void) {
    int last;

    /* TimerControl is a countdown gate.  A nonzero value decrements here;
     * only a value that remains nonzero skips DecTimers. */
    if (g_TimerControl != 0) {
        g_TimerControl--;
        if (g_TimerControl != 0) return;
    }

    g_IntervalTimerControl--;
    last = TIMER_PLAYER_ANIM + 0x13; /* X=$14, offsets $00..$14 */
    if ((g_IntervalTimerControl & 0x80) != 0) {
        g_IntervalTimerControl = INTERVAL_TIMER_RELOAD;
        last = TIMER_RESERVED_35;    /* X=$23, offsets $00..$23 */
    }

    for (; last >= 0; last--) {
        if (g_Timers[last] != 0) g_Timers[last]--;
    }
}

void Timers_ClearAreaBank(void) {
    /* ClrTimersLoop starts at X=$21 and clears down through zero. */
    memset(g_Timers, 0, TIMER_DEMO);
}
