/* timers.h - ownership of the NES Timers bank and DecTimers routine */
#ifndef SMB_TIMERS_H
#define SMB_TIMERS_H

/* DecTimers (smb1-disasm/system/nmi.asm:74-93). */
void Timers_DecTimers(void);

/* InitializeArea's ClrTimersLoop (main.asm:1417-1425): clear $0780-$07A1,
 * while preserving DemoTimer and the adjacent byte visited by DecTimers. */
void Timers_ClearAreaBank(void);

#endif
