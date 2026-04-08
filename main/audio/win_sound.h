#ifndef WIN_SOUND_H
#define WIN_SOUND_H

#include <stdint.h>

// Melodia de vitoria de rodada: arpejo ascendente de Do Maior (~0.8s)
// C5 → E5 → G5 → C6 (som alegre e conciso)
#define WIN_SOUND_LENGTH 8800

extern uint8_t win_sound_data[WIN_SOUND_LENGTH];

#endif
