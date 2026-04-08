#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"

#include "ili9341.h"
#include "gfx_ili9341.h"

#include "audio/sample.h"
#include "audio/tones.h"
#include "audio/lost_music.h"
#include "audio/win_sound.h"

// ── Pinos (remapeados para evitar conflito com LCD SPI) ─────────────────────
// LCD usa: GP16(RST), GP17(CS), GP18(SCK), GP19(MOSI), GP22(DC)

#define AUDIO_PIN       14   // era 22 (agora LCD DC)

#define BTN_VERMELHO    2    // era 18 (agora LCD SCK)
#define BTN_AZUL        3    // era 19 (agora LCD MOSI)
#define BTN_VERDE       4    // era 16 (GP0/GP1 ocupados)
#define BTN_AMARELO     5    // era 17 (GP0/GP1 ocupados)

#define LED_VERMELHO    11   // sem conflito
#define LED_AZUL        13   // sem conflito
#define LED_VERDE       20   // sem conflito
#define LED_AMARELO     21   // sem conflito

#define LED_START       6    // LED indicador do START
#define BTN_START       15   // sem conflito

#define DEBOUNCE_MS     200

// ── Constantes do jogo ──────────────────────────────────────────────────────
#define MAX_SEQ          32
#define GAP_MS           300
#define INPUT_TIMEOUT_MS 5000

#define COLOR_AZUL       0
#define COLOR_AMARELO    1
#define COLOR_VERMELHO   2
#define COLOR_VERDE      3

#define MSG_TIMEOUT      0xFF
#define MSG_TICK_BASE    0x10

// ── Layout LCD (240x320, rotacao 0 = retrato) ──────────────────────────────
#define SCREEN_W  240
#define SCREEN_H  320
#define TITLE_Y       20
#define SCORE_LABEL_Y 100
#define SCORE_VALUE_Y 140
#define STATUS_Y      250

// ── Tabelas de lookup ───────────────────────────────────────────────────────
static const uint LED_PINS[] = { LED_AZUL, LED_AMARELO, LED_VERMELHO, LED_VERDE };
static const uint BTN_PINS[] = { BTN_AZUL, BTN_AMARELO, BTN_VERMELHO, BTN_VERDE };
static const char *COLOR_NAMES[] = { "AZUL", "AMARELO", "VERMELHO", "VERDE" };

static const uint8_t *SOUNDS[]  = { tone_azul, tone_amarelo, tone_vermelho, tone_verde };
static const int      SOUND_LEN[] = {
    TONE_SAMPLES, TONE_SAMPLES, TONE_SAMPLES, TONE_SAMPLES
};

// ── Estado do jogo ──────────────────────────────────────────────────────────
static int sequence[MAX_SEQ];
static int seq_length;

// ── Estado do player de audio ───────────────────────────────────────────────
static volatile int            is_playing = 0;
static volatile int            wav_pos    = 0;
static volatile const uint8_t *wav_buf    = WAV_DATA_1;
static volatile int            wav_len    = WAV_DATA_1_LENGTH;

// ── Musica de fundo (loop) ─────────────────────────────────────────────────
static volatile int            bg_playing = 0;
static volatile int            bg_pos     = 0;
static volatile const uint8_t *bg_buf     = WAV_DATA_1;
static volatile int            bg_len     = WAV_DATA_1_LENGTH;

// ── Flags de botao ──────────────────────────────────────────────────────────
static volatile int flag_azul     = 0;
static volatile int flag_amarelo  = 0;
static volatile int flag_vermelho = 0;
static volatile int flag_verde    = 0;
static volatile int flag_start   = 0;

// ── Countdown (hardware timer alarm) ────────────────────────────────────────
static volatile int  countdown_remaining = 0;
static volatile bool countdown_tick      = false;
static alarm_id_t    countdown_alarm_id  = 0;

// ── Estado LCD ──────────────────────────────────────────────────────────────
static int  current_score = 0;
static char current_status[32] = "AGUARDANDO...";

// ── IRQ: botoes ─────────────────────────────────────────────────────────────
void btn_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_AZUL)     flag_azul     = 1;
    if (gpio == BTN_AMARELO)  flag_amarelo  = 1;
    if (gpio == BTN_VERMELHO) flag_vermelho = 1;
    if (gpio == BTN_VERDE)    flag_verde    = 1;
    if (gpio == BTN_START)   flag_start   = 1;
}

// ── Alarm callback: countdown de 1 em 1 segundo ────────────────────────────
int64_t countdown_alarm_callback(alarm_id_t id, void *user_data) {
    if (countdown_remaining > 0) {
        countdown_tick = true;
        countdown_remaining--;
        return -1000000;
    }
    return 0;
}

// ── IRQ: PWM – saida de audio ───────────────────────────────────────────────
void pwm_interrupt_handler() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));

    if (is_playing) {
        pwm_set_gpio_level(AUDIO_PIN, wav_buf[wav_pos >> 3]);
        wav_pos++;
        if (wav_pos >= (wav_len << 3)) {
            wav_pos    = 0;
            is_playing = 0;
        }
        return;
    }

    if (bg_playing) {
        pwm_set_gpio_level(AUDIO_PIN, bg_buf[bg_pos >> 3]);
        bg_pos++;
        if (bg_pos >= (bg_len << 3)) {
            bg_pos = 0;
        }
        return;
    }

    pwm_set_gpio_level(AUDIO_PIN, 0);
}

// ── LCD: funcoes de desenho (direto, sem UART) ──────────────────────────────

void draw_title() {
    gfx_setTextSize(3);
    gfx_setTextColor(ILI9341_CYAN);
    const char *title = "GENIUS";
    int tw = gfx_getTextWidth(title);
    gfx_drawText((SCREEN_W - tw) / 2, TITLE_Y, title);
}

void draw_score_label() {
    gfx_setTextSize(2);
    gfx_setTextColor(ILI9341_WHITE);
    const char *label = "PONTOS";
    int lw = gfx_getTextWidth(label);
    gfx_drawText((SCREEN_W - lw) / 2, SCORE_LABEL_Y, label);
}

void draw_score(int score) {
    current_score = score;
    gfx_fillRect(0, SCORE_VALUE_Y, SCREEN_W, 50, ILI9341_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", score);
    gfx_setTextSize(5);
    gfx_setTextColor(ILI9341_YELLOW);
    int sw = gfx_getTextWidth(buf);
    gfx_drawText((SCREEN_W - sw) / 2, SCORE_VALUE_Y, buf);
}

void draw_status(const char *status) {
    strncpy(current_status, status, sizeof(current_status) - 1);
    current_status[sizeof(current_status) - 1] = '\0';
    gfx_fillRect(0, STATUS_Y, SCREEN_W, 50, ILI9341_BLACK);
    gfx_setTextSize(2);

    if (strcmp(status, "ERROU!") == 0 || strcmp(status, "TEMPO ESGOTADO!") == 0 || strcmp(status, "GAME OVER") == 0)
        gfx_setTextColor(ILI9341_RED);
    else if (strcmp(status, "VITORIA!") == 0)
        gfx_setTextColor(ILI9341_GREEN);
    else if (strcmp(status, "SUA VEZ!") == 0)
        gfx_setTextColor(ILI9341_YELLOW);
    else
        gfx_setTextColor(ILI9341_WHITE);

    int tw = gfx_getTextWidth(status);
    gfx_drawText((SCREEN_W - tw) / 2, STATUS_Y, status);
}

void draw_full_screen() {
    gfx_clear();
    draw_title();
    draw_score_label();
    draw_score(current_score);
    draw_status(current_status);
}

// ── Helpers ─────────────────────────────────────────────────────────────────
void leds_off() {
    for (int i = 0; i < 4; i++)
        gpio_put(LED_PINS[i], 0);
}

void play(const uint8_t *buf, int len) {
    irq_set_enabled(PWM_IRQ_WRAP, false);
    wav_buf    = buf;
    wav_len    = len;
    wav_pos    = 0;
    is_playing = 1;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void wait_audio_done() {
    while (is_playing)
        tight_loop_contents();
}

void bg_music_start(const uint8_t *buf, int len) {
    irq_set_enabled(PWM_IRQ_WRAP, false);
    bg_buf     = buf;
    bg_len     = len;
    bg_pos     = 0;
    bg_playing = 1;
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

void bg_music_stop() {
    bg_playing = 0;
}

void clear_flags() {
    flag_azul     = 0;
    flag_amarelo  = 0;
    flag_vermelho = 0;
    flag_verde    = 0;
}

void wait_start_button() {
    flag_start = 0;
    uint32_t last_toggle = time_us_32();
    bool led_on = false;
    while (!flag_start) {
        uint32_t now = time_us_32();
        if ((now - last_toggle) >= 250000) {
            led_on = !led_on;
            gpio_put(LED_START, led_on);
            last_toggle = now;
        }
        tight_loop_contents();
    }
    gpio_put(LED_START, 1);
    flag_start = 0;
    sleep_ms(DEBOUNCE_MS);
}

// ── Funcoes do jogo ─────────────────────────────────────────────────────────

void play_color(int color) {
    printf("  LED %s ON\n", COLOR_NAMES[color]);
    leds_off();
    gpio_put(LED_PINS[color], 1);

    int len = SOUND_LEN[color];
    if (len > TONE_SAMPLES)
        len = TONE_SAMPLES;

    play(SOUNDS[color], len);
    wait_audio_done();

    leds_off();
    sleep_ms(GAP_MS);
}

// ── Core 1: monitor de input + countdown ─────────────────────────────────
void core1_main() {
    while (1) {
        uint32_t timeout_ms = multicore_fifo_pop_blocking();

        clear_flags();
        countdown_remaining = timeout_ms / 1000;
        countdown_tick = false;

        multicore_fifo_push_blocking(MSG_TICK_BASE | countdown_remaining);

        countdown_alarm_id = add_alarm_in_ms(1000, countdown_alarm_callback, NULL, false);

        uint32_t start = time_us_32();
        uint32_t timeout_us = timeout_ms * 1000;
        bool done = false;

        while (!done && (time_us_32() - start) < timeout_us) {
            if (countdown_tick) {
                countdown_tick = false;
                multicore_fifo_push_blocking(MSG_TICK_BASE | countdown_remaining);
            }

            if (flag_azul)          { flag_azul = 0;     cancel_alarm(countdown_alarm_id); multicore_fifo_push_blocking(COLOR_AZUL);     done = true; }
            else if (flag_amarelo)  { flag_amarelo = 0;  cancel_alarm(countdown_alarm_id); multicore_fifo_push_blocking(COLOR_AMARELO);  done = true; }
            else if (flag_vermelho) { flag_vermelho = 0; cancel_alarm(countdown_alarm_id); multicore_fifo_push_blocking(COLOR_VERMELHO); done = true; }
            else if (flag_verde)    { flag_verde = 0;    cancel_alarm(countdown_alarm_id); multicore_fifo_push_blocking(COLOR_VERDE);    done = true; }

            tight_loop_contents();
        }

        if (!done) {
            cancel_alarm(countdown_alarm_id);
            multicore_fifo_push_blocking(MSG_TIMEOUT);
        }

        clear_flags();
    }
}

// ── wait_button: Core 0 envia comando e le resultados do FIFO ────────────
int wait_button(uint32_t timeout_ms) {
    multicore_fifo_push_blocking(timeout_ms);

    while (1) {
        uint32_t msg = multicore_fifo_pop_blocking();

        if ((msg & 0xF0) == MSG_TICK_BASE) {
            printf("  [TIMER] %d\n", msg & 0x0F);
        } else if (msg == MSG_TIMEOUT) {
            printf("  TIMEOUT!\n");
            return -1;
        } else {
            printf("  BTN %s\n", COLOR_NAMES[msg]);
            sleep_ms(DEBOUNCE_MS);
            return msg;
        }
    }
}

void error_feedback() {
    printf("ERRO!\n");
    leds_off();
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_VERMELHO, 1);
        sleep_ms(200);
        gpio_put(LED_VERMELHO, 0);
        sleep_ms(200);
    }
    gpio_put(LED_VERMELHO, 1);
    sleep_ms(500);
    gpio_put(LED_VERMELHO, 0);
}

void success_feedback() {
    printf("Rodada OK!\n");
    play(win_sound_data, WIN_SOUND_LENGTH);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++)
            gpio_put(LED_PINS[j], 1);
        sleep_ms(100);
        leds_off();
        sleep_ms(100);
    }
    wait_audio_done();
}

void startup_animation() {
    leds_off();
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 4; i++) {
            gpio_put(LED_PINS[i], 1);
            sleep_ms(150);
            gpio_put(LED_PINS[i], 0);
        }
    }
    for (int j = 0; j < 4; j++)
        gpio_put(LED_PINS[j], 1);
    sleep_ms(300);
    leds_off();
    sleep_ms(500);
}

void victory_animation() {
    printf("VITORIA! Completou %d rodadas!\n", MAX_SEQ);
    for (int round = 0; round < 5; round++) {
        for (int j = 0; j < 4; j++)
            gpio_put(LED_PINS[j], 1);
        sleep_ms(150);
        leds_off();
        sleep_ms(150);
    }
    play(SOUNDS[COLOR_VERDE], SOUND_LEN[COLOR_VERDE]);
    wait_audio_done();
}

// ── main ────────────────────────────────────────────────────────────────────
int main(void) {
    set_sys_clock_khz(176000, true);
    stdio_init_all();

    printf("\n== GENIUS (single Pico) ==\n");

    // LCD
    LCD_initDisplay();
    LCD_setRotation(0);
    gfx_init();
    draw_full_screen();
    printf("LCD init OK\n");

    // LEDs
    for (int i = 0; i < 4; i++) {
        gpio_init(LED_PINS[i]);
        gpio_set_dir(LED_PINS[i], GPIO_OUT);
    }
    leds_off();

    // Botoes
    for (int i = 0; i < 4; i++) {
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_pull_up(BTN_PINS[i]);
    }

    // LED START
    gpio_init(LED_START);
    gpio_set_dir(LED_START, GPIO_OUT);
    gpio_put(LED_START, 0);

    // Botao START
    gpio_init(BTN_START);
    gpio_set_dir(BTN_START, GPIO_IN);
    gpio_pull_up(BTN_START);

    // IRQ dos botoes
    gpio_set_irq_enabled_with_callback(BTN_AZUL, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(BTN_AMARELO,  GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_VERMELHO, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_VERDE,    GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_START,    GPIO_IRQ_EDGE_FALL, true);

    // PWM para audio
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int pwm_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_clear_irq(pwm_slice);
    pwm_set_irq_enabled(pwm_slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 8.0f);
    pwm_config_set_wrap(&cfg, 250);
    pwm_init(pwm_slice, &cfg, true);
    pwm_set_gpio_level(AUDIO_PIN, 0);

    generate_tones();
    amplify_audio(WAV_DATA_1, WAV_DATA_1_LENGTH);

    printf("Init OK\n");
    multicore_launch_core1(core1_main);
    bg_music_start(WAV_DATA_1, WAV_DATA_1_LENGTH);

    // ── Game loop ───────────────────────────────────────────────────────────
    int game_num = 0;
    while (1) {
        printf("\nPressione START para jogar...\n");
        draw_status("PRESSIONE START");
        wait_start_button();
        bg_music_start(WAV_DATA_1, WAV_DATA_1_LENGTH);

        game_num++;
        printf("\n--- Jogo %d ---\n", game_num);

        srand(time_us_32());
        draw_score(0);
        draw_status("GENIUS");
        startup_animation();

        for (int i = 0; i < MAX_SEQ; i++)
            sequence[i] = rand() % 4;

        seq_length = 1;
        int round_num = 1;
        bool game_over = false;

        while (!game_over && seq_length <= MAX_SEQ) {
            printf("\nRodada %d (LEDs: %d):\n", round_num, seq_length);

            printf("Seq: ");
            for (int i = 0; i < seq_length; i++)
                printf("%s ", COLOR_NAMES[sequence[i]]);
            printf("\n");

            draw_status("OBSERVE...");
            sleep_ms(500);
            for (int i = 0; i < seq_length; i++)
                play_color(sequence[i]);

            printf("Sua vez:\n");
            draw_status("SUA VEZ!");
            clear_flags();
            for (int i = 0; i < seq_length; i++) {
                int pressed = wait_button(INPUT_TIMEOUT_MS);

                if (pressed < 0) {
                    printf("  Timeout! Esperava %s\n", COLOR_NAMES[sequence[i]]);
                    draw_status("TEMPO ESGOTADO!");
                    error_feedback();
                    game_over = true;
                    break;
                }

                if (pressed != sequence[i]) {
                    printf("  Errou! %s != %s\n", COLOR_NAMES[pressed], COLOR_NAMES[sequence[i]]);
                    draw_status("ERROU!");
                    error_feedback();
                    game_over = true;
                    break;
                }

                printf("  OK %d/%d\n", i + 1, seq_length);
                play_color(pressed);
            }

            if (!game_over) {
                draw_score(round_num);
                success_feedback();
                round_num++;
                if (round_num <= 3) {
                    seq_length = round_num;
                } else if (round_num == 4) {
                    seq_length = 6;
                } else {
                    seq_length += 2;
                }
            }
        }

        gpio_put(LED_START, 0);

        if (!game_over) {
            draw_status("VITORIA!");
            draw_score(round_num - 1);
            victory_animation();
        } else {
            draw_score(round_num - 1);
            draw_status("GAME OVER");
            bg_music_start(lost_music_data, LOST_MUSIC_LENGTH);
        }

        printf("Pontos: %d\n", round_num - 1);
    }
}
