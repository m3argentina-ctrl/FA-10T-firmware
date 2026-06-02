#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "nvs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Number of stages in a programs-mode recipe
#define PROG_STAGE_COUNT      3
#define PROG_NAME_MAX         20
#define MODEL_NAME_MAX        16
#define SERIE_NUM_MAX         16     // número de serie del equipo (ej. "2026-A-0123")
#define PIN_LEN_MAX           5      // 4 digits + NUL
#define MODULOS_MAX           16     // tope de módulos turbina+resistencia (línea comercial)

typedef struct {
    float    temperature;     // °C, after calibration
    float    raw_temperature; // °C, pre-calibration
    bool     fault;
    uint8_t  fault_status;
    uint64_t timestamp_us;
} sensor_sample_t;

typedef enum {
    OP_MODE_IDLE      = 0,
    OP_MODE_MANUAL    = 1,
    OP_MODE_PROGRAMS  = 2,
} op_mode_t;

typedef enum {
    RUN_STATE_IDLE         = 0,
    RUN_STATE_CONFIGURING  = 1,
    RUN_STATE_RUNNING      = 2,
    RUN_STATE_PAUSED       = 3,
    RUN_STATE_COMPLETED    = 4,
    RUN_STATE_ALARM        = 5,
} run_state_t;

typedef struct {
    sensor_sample_t last_sample;
    float           setpoint;            // user-configured target (during programming)
    float           pid_output;
    float           ssr_drv_duty;
    float           ssr_fan_duty;
    float           ssr_aux_duty;
    uint32_t        safety_faults;
    uint32_t        uptime_s;
    bool            running;             // legacy field: true while not tripped + session active

    // Humidity / fan / SHT31
    float           humidity;
    float           sht31_temp;
    bool            humidity_fault;
    float           fan_current;
    float           fan_nominal;
    bool            fan_nominal_known;
    bool            fan_fault;

    // --- Operation / session state ----------------------------------------
    op_mode_t       op_mode;
    run_state_t     run_state;

    // PID gating: control_task drives heat against effective_setpoint when
    // fan_command_on is true. modo_manual / programa update both.
    float           effective_setpoint;
    bool            fan_command_on;

    // 3-stage recipe shared by MANUAL (stage 0 only) and PROGRAMS (all 3).
    uint8_t         etapa_activa;
    float           etapa_sp[PROG_STAGE_COUNT];
    uint32_t        etapa_duration_s[PROG_STAGE_COUNT];

    uint32_t        session_elapsed_s;
    uint32_t        session_total_s;
    uint32_t        session_remaining_s;
    float           t_min_sesion;
    float           t_max_sesion;
    // Consumo energético de la sesión, estimado integrando el duty del PID.
    // Valores POR MÓDULO (una resistencia de 2000 W / una turbina): la nube los
    // multiplica por la cantidad de módulos del modelo. Acumuladores float para
    // no truncar dt<1s (mismo motivo que session_elapsed_s).
    float           session_energy_wh;   // Wh de UNA resistencia de 2000 W
    float           session_fan_on_s;    // segundos de UNA turbina encendida
    // warm-up: el tiempo de proceso NO empieza a correr hasta que la T
    // actual cruza por primera vez (effective_setpoint - SAFETY_HYSTERESIS_C).
    // Mientras !warmup_done la UI muestra "CALENTANDO" y session_elapsed_s
    // queda en 0. Se setea una sola vez por sesión (la primera vez que
    // alcanza SP); en programas no se resetea entre etapas.
    bool            warmup_done;

    char            nombre_programa[PROG_NAME_MAX];

    // Identity / config exposed in AREA TECNICA
    char            modelo[MODEL_NAME_MAX];
    char            serie[SERIE_NUM_MAX];
    char            pin_servicio[PIN_LEN_MAX];
    // Cantidad de módulos turbina+resistencia (2000 W c/u). La nube multiplica el
    // consumo por-módulo (session_energy_wh / session_fan_on_s) por este valor.
    uint8_t         num_modulos;
} app_state_t;

void              app_state_init(void);
QueueHandle_t     app_state_sensor_queue(void);
void              app_state_lock(void);
void              app_state_unlock(void);
app_state_t      *app_state_get(void);   // call between lock/unlock

void              app_state_copy_config(fa10t_config_t *out);
void              app_state_set_config(const fa10t_config_t *cfg);

// Identidad persistente (NVS namespace "fa10t_id"). Sobreviven reboots.
// Si NVS está vacío, app_state_init() deja los defaults hardcodeados.
esp_err_t         app_state_save_modelo(const char *modelo);
esp_err_t         app_state_save_serie(const char *serie);
esp_err_t         app_state_save_pin(const char *pin);
esp_err_t         app_state_save_num_modulos(uint8_t n);   // clamp 1..MODULOS_MAX

#ifdef __cplusplus
}
#endif
