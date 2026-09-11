#ifndef SMB_ENGINE_AUDIO_H
#define SMB_ENGINE_AUDIO_H

#include <stdint.h>
#include "audio-verifier.h"

/* SoundEngine, main.asm:12435-13034 and audio/engine.asm. */
void Audio_SoundEngine(void);

/* InitializeMemory clears the low-page sound fields on every area load;
 * InitializeGame additionally clears SoundMemory ($07b0-$07cf). */
void Audio_ResetLowMemory(void);
void Audio_ResetGameMemory(void);

void Audio_GetVerifierState(AudioVerifierState *state);
int Audio_LoadRomTables(void);

#endif
