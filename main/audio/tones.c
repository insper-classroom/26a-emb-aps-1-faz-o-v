#include "tones.h"
#include "lost_music.h"
#include "win_sound.h"

uint8_t tone_azul[TONE_SAMPLES];
uint8_t tone_amarelo[TONE_SAMPLES];
uint8_t tone_vermelho[TONE_SAMPLES];
uint8_t tone_verde[TONE_SAMPLES];

uint8_t lost_music_data[LOST_MUSIC_LENGTH];
uint8_t win_sound_data[WIN_SOUND_LENGTH];

void generate_tones(void) {
    // Onda quadrada: metade do periodo alto (200), metade baixo (50)
    // Periodo = 11000 / frequencia
    for (int i = 0; i < TONE_SAMPLES; i++) {
        tone_azul[i]     = (i % 22 < 11) ? 200 : 50;  // ~500Hz
        tone_amarelo[i]  = (i % 28 < 14) ? 200 : 50;  // ~400Hz
        tone_vermelho[i] = (i % 18 <  9) ? 200 : 50;  // ~600Hz
        tone_verde[i]    = (i % 16 <  8) ? 200 : 50;  // ~700Hz
    }

    // Melodia de vitoria: arpejo ascendente (C5 → E5 → G5 → C6)
    {
        int pos = 0;
        struct { int period; int duration; } win_notes[] = {
            { 21, 1650 },   // C5 ~523Hz  150ms
            {  0,  330 },   // pausa       30ms
            { 17, 1650 },   // E5 ~659Hz  150ms
            {  0,  330 },   // pausa       30ms
            { 14, 1650 },   // G5 ~784Hz  150ms
            {  0,  330 },   // pausa       30ms
            { 11, 2200 },   // C6 ~1047Hz 200ms (sustentada)
            {  0,  330 },   // pausa       30ms
        };
        int num_win = sizeof(win_notes) / sizeof(win_notes[0]);

        for (int n = 0; n < num_win && pos < WIN_SOUND_LENGTH; n++) {
            for (int s = 0; s < win_notes[n].duration && pos < WIN_SOUND_LENGTH; s++, pos++) {
                if (win_notes[n].period == 0) {
                    win_sound_data[pos] = 125;
                } else {
                    int half = win_notes[n].period / 2;
                    win_sound_data[pos] = (s % win_notes[n].period < half) ? 200 : 50;
                }
            }
        }
        // Preencher restante com silencio
        while (pos < WIN_SOUND_LENGTH)
            win_sound_data[pos++] = 125;
    }

    // Melodia de derrota: notas descendentes (G4 → E4 → C4 → A3)
    // Cada nota é onda quadrada com pausa entre elas
    // Estrutura: nota(250ms) + pausa(50ms) repetido, ultima nota mais longa
    int pos = 0;
    struct { int period; int duration; } notes[] = {
        { 28, 2750 },   // G4 ~392Hz  250ms
        {  0,  550 },   // pausa       50ms
        { 33, 2750 },   // E4 ~330Hz  250ms
        {  0,  550 },   // pausa       50ms
        { 42, 2750 },   // C4 ~262Hz  250ms
        {  0,  550 },   // pausa       50ms
        { 50, 5500 },   // A3 ~220Hz  500ms (sustentada)
        {  0, 1100 },   // pausa      100ms
    };
    int num_notes = sizeof(notes) / sizeof(notes[0]);

    for (int n = 0; n < num_notes && pos < LOST_MUSIC_LENGTH; n++) {
        for (int s = 0; s < notes[n].duration && pos < LOST_MUSIC_LENGTH; s++, pos++) {
            if (notes[n].period == 0) {
                lost_music_data[pos] = 125; // silencio
            } else {
                int half = notes[n].period / 2;
                lost_music_data[pos] = (s % notes[n].period < half) ? 200 : 50;
            }
        }
    }
}

void amplify_audio(uint8_t *buf, int len) {
    // Amplifica audio: estica valores em torno do centro (125)
    // Fator 1.6x = volume ~60% mais alto
    for (int i = 0; i < len; i++) {
        int val = 125 + (int)((buf[i] - 125) * 1.6f);
        if (val < 0) val = 0;
        if (val > 250) val = 250;
        buf[i] = (uint8_t)val;
    }
}
