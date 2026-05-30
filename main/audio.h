#pragma once

// Driver de audio mínimo para el codec ES8311 + amp NS4150 del Waveshare-3.5.
// Solo playback de tonos cortos (clicks de teclado, beeps de alarma).
//
// Para no bloquear el hilo de UI por más de ~10 ms, las primitivas son
// asíncronas: postean a una task interna que escribe al codec.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa I2S + codec + enciende el amplificador. Debe llamarse DESPUÉS de
// display_init() (porque usa el TCA9554 para PA_CTRL y el bus I2C compartido).
esp_err_t audio_init(void);

// Click rápido de feedback de tecla (tono fijo, ~2 kHz, ~25 ms).
// Asíncrono: regresa enseguida. Si la cola está llena, descarta.
void audio_click(void);

// Tono custom. freq_hz: 100..8000, dur_ms: 5..500. Asíncrono.
void audio_beep(uint16_t freq_hz, uint16_t dur_ms);

// Secuencia de N tonos iguales con silencio entre cada uno.
// n_repeats: 1..8, gap_ms: 0..1000.
void audio_beep_seq(uint16_t freq_hz, uint16_t dur_ms,
                     uint8_t n_repeats, uint16_t gap_ms);

// Patrón estándar de "fin de ciclo": 3 beeps de 300 ms @ 800 Hz, gap 150 ms.
// Usado por modo_manual y programa cuando la sesión transiciona a COMPLETED.
void audio_alarm_done(void);

// Volumen global del codec (0..100). audio_init() arranca en ~50.
void audio_set_volume(uint8_t percent);

#ifdef __cplusplus
}
#endif
