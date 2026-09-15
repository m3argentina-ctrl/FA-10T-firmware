#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    AUTOTUNE_IDLE = 0,
    AUTOTUNE_HEATING,     // primera llegada al SP de prueba
    AUTOTUNE_RELAY,       // oscilando alrededor del SP
    AUTOTUNE_DONE,        // ganancias listas, esperando ACEPTAR / DESCARTAR
    AUTOTUNE_ABORTED,
} autotune_state_t;

typedef struct {
    autotune_state_t state;
    float    sp;
    float    temp;
    float    duty;          // salida actual del relé (0..1)
    int      cycles;        // ciclos medidos (se piden AUTOTUNE_CYCLES parejos, no un total)
    float    spread;        // diferencia entre los últimos AUTOTUNE_CYCLES (0,31 = 31 %)
    float    elapsed_s;
    float    kp, ki, kd;    // resultado (DONE)
    float    ku, pu;        // ganancia última y período promedio (DONE)
    char     reason[40];    // motivo (ABORTED)
    bool     cooling;       // turbinas ventilando después de terminar (no tras un CANCELAR)
} autotune_status_t;

void      autotune_init(void);
esp_err_t autotune_start(float sp);             // ESP_ERR_INVALID_STATE: proceso, alarma o sonda en falla
// Corte pedido por una persona (UI, web, alarma): thread-safe, se aplica en el próximo
// ciclo de control y apaga resistencias Y turbinas (sin ventilación posterior).
void      autotune_cancel(const char *reason);
void      autotune_ack(void);                   // DONE/ABORTED → IDLE (tras ACEPTAR / DESCARTAR / CERRAR)
void      autotune_get_status(autotune_status_t *out);
bool      autotune_is_running(void);            // HEATING o RELAY: el autotune maneja las resistencias
bool      autotune_fans_on(void);               // corriendo o ventilando después de terminar

// control_task, cada ciclo: devuelve el duty de las resistencias (0 si no corre).
float     autotune_step(float temp, bool sensor_fault, bool session_active, float dt);

// Si las ganancias activas vienen de un autotune (AREA TECNICA / web local).
void      autotune_set_tuned(bool tuned);
bool      autotune_is_tuned(void);
