#ifndef TONES_H
#define TONES_H

#include <stdint.h>

#define TONE_SAMPLES 5500  // ~500ms a 11kHz

// Tons de onda quadrada por cor (frequencias distintas)
extern uint8_t tone_azul[TONE_SAMPLES];       // ~500Hz
extern uint8_t tone_amarelo[TONE_SAMPLES];     // ~400Hz
extern uint8_t tone_vermelho[TONE_SAMPLES];    // ~600Hz
extern uint8_t tone_verde[TONE_SAMPLES];       // ~700Hz

// Preenche todos os arrays de tons e a melodia de derrota
void generate_tones(void);

// Amplifica audio in-place (estica valores em torno de 125)
void amplify_audio(uint8_t *buf, int len);

#endif
