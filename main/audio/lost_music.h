#ifndef LOST_MUSIC_H
#define LOST_MUSIC_H

#include <stdint.h>

// Melodia de game over: sequencia descendente em loop (~1.5s)
// G4 → E4 → C4 → A3 (escala descendente, tom triste)
#define LOST_MUSIC_LENGTH 16500

extern uint8_t lost_music_data[LOST_MUSIC_LENGTH];

#endif
