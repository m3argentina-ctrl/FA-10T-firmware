#include "autotune.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "app_state.h"
#include "safety.h"
#include "telemetry.h"

static const char *TAG = "autotune";

#define AT_PI  3.14159265f

typedef struct {
    autotune_state_t state;
    float sp, temp, elapsed_s;
    bool  relay_on;
    float bias, d;                  // relé con sesgo: salida = bias ± d
    float t_switch, t_on, t_off, t_high;
    int   n_on, n_small;
    float max_t, min_t;
    int   n;                        // ciclos registrados
    float ku[AUTOTUNE_MAX_CYCLES];
    float pu[AUTOTUNE_MAX_CYCLES];
    float kp, ki, kd, ku_avg, pu_avg;
    char  reason[40];
    bool  cancel_req;
    char  cancel_reason[40];
    bool  cooling;
    float cool_s;
} at_t;

static at_t              s;
static SemaphoreHandle_t s_mtx;
static volatile bool     s_tuned;

void autotune_init(void)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
}

static void lock(void)   { xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mtx); }

static bool running_locked(void)
{
    return s.state == AUTOTUNE_HEATING || s.state == AUTOTUNE_RELAY;
}

static void finish_locked(autotune_state_t st, const char *reason)
{
    s.state    = st;
    s.relay_on = false;
    s.cooling  = true;               // ventila después, igual que tras un proceso
    s.cool_s   = 0.0f;
    snprintf(s.reason, sizeof s.reason, "%s", reason ? reason : "");
}

// Tyreus-Luyben sobre los últimos ciclos: conservador, prioriza no pasarse del SP.
static void compute_locked(void)
{
    const int n0 = s.n - AUTOTUNE_CYCLES;
    float kmin = s.ku[n0], kmax = kmin, pmin = s.pu[n0], pmax = pmin, ksum = 0.0f, psum = 0.0f;
    for (int i = n0; i < s.n; ++i) {
        kmin = fminf(kmin, s.ku[i]);
        kmax = fmaxf(kmax, s.ku[i]);
        pmin = fminf(pmin, s.pu[i]);
        pmax = fmaxf(pmax, s.pu[i]);
        ksum += s.ku[i];
        psum += s.pu[i];
    }
    const bool steady = (kmax <= 1.25f * kmin) && (pmax <= 1.25f * pmin);
    if (!steady && s.n < AUTOTUNE_MAX_CYCLES) return;
    if (!steady && (kmax > 1.5f * kmin || pmax > 1.5f * pmin)) {
        finish_locked(AUTOTUNE_ABORTED, "OSCILACION IRREGULAR");
        return;
    }
    s.ku_avg = ksum / AUTOTUNE_CYCLES;
    s.pu_avg = psum / AUTOTUNE_CYCLES;
    const float kp = s.ku_avg / 2.2f;
    const float ti = 2.2f * s.pu_avg;
    const float td = s.pu_avg / 6.3f;
    s.kp = kp;
    s.ki = kp / ti;
    s.kd = kp * td;
    finish_locked(AUTOTUNE_DONE, "");
}

static void record_locked(float ku, float pu)
{
    if (s.n < AUTOTUNE_MAX_CYCLES) {
        s.ku[s.n] = ku;
        s.pu[s.n] = pu;
        s.n++;
    }
    if (s.n >= AUTOTUNE_CYCLES) compute_locked();
}

// Relé con histéresis y sesgo (método de Åström-Hägglund con el ajuste de sesgo que
// usa Marlin): el sesgo iguala los tiempos ON/OFF porque la cámara calienta mucho más
// rápido de lo que enfría. Ku sale de la amplitud de cada ciclo completo (ON + OFF).
static float relay_step_locked(float temp)
{
    const float h = AUTOTUNE_HYST_C;
    if (temp > s.max_t) s.max_t = temp;
    if (temp < s.min_t) s.min_t = temp;
    const bool can_switch = (s.elapsed_s - s.t_switch) >= AUTOTUNE_MIN_PHASE_S;

    if (s.relay_on && temp >= s.sp + h && can_switch) {
        s.relay_on = false;
        s.t_switch = s.elapsed_s;
        s.t_high   = s.elapsed_s - s.t_on;
        s.t_off    = s.elapsed_s;
        s.max_t    = temp;
        if (s.state == AUTOTUNE_HEATING) s.state = AUTOTUNE_RELAY;
    } else if (!s.relay_on && temp <= s.sp - h && can_switch) {
        s.relay_on = true;
        s.t_switch = s.elapsed_s;
        const float t_low = s.elapsed_s - s.t_off;
        if (s.n_on >= 1) {
            const float a = 0.5f * (s.max_t - s.min_t);
            if (a > 1.05f * h) {
                record_locked(4.0f * s.d / (AT_PI * sqrtf(a * a - h * h)), s.t_high + t_low);
                if (!running_locked()) return 0.0f;
            } else if (++s.n_small >= 3) {
                finish_locked(AUTOTUNE_ABORTED, "OSCILACION MUY CHICA");
                return 0.0f;
            }
            s.bias += s.d * (s.t_high - t_low) / (s.t_high + t_low);
            s.bias  = fminf(fmaxf(s.bias, 0.1f), 0.9f);
            s.d     = fminf(s.bias, 1.0f - s.bias);
        }
        s.n_on++;
        s.t_on  = s.elapsed_s;
        s.min_t = temp;
    }
    return s.relay_on ? s.bias + s.d : s.bias - s.d;
}

esp_err_t autotune_start(float sp)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    if (sp < AUTOTUNE_SP_MIN_C || sp > AUTOTUNE_SP_MAX_C) return ESP_ERR_INVALID_ARG;

    app_state_lock();
    const app_state_t *st = app_state_get();
    const bool ok = (st->op_mode == OP_MODE_IDLE) && (st->run_state != RUN_STATE_ALARM) &&
                    !(st->safety_faults & SAFETY_TRIP_MASK) &&
                    !st->last_sample.fault && !st->last_sample.heater_fault;
    const float t0 = st->last_sample.temperature;
    app_state_unlock();
    if (!ok) return ESP_ERR_INVALID_STATE;

    lock();
    if (running_locked()) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s, 0, sizeof s);
    s.state    = AUTOTUNE_HEATING;
    s.sp       = sp;
    s.temp     = t0;
    s.relay_on = (t0 < sp);
    s.bias     = 0.5f;
    s.d        = 0.5f;
    s.max_t    = t0;
    s.min_t    = t0;
    unlock();

    ESP_LOGW(TAG, "autotune iniciado: SP=%.1f T=%.1f", sp, t0);
    telemetry_log_event(TELEM_EVT_SERVICE, "autotune inicio SP %.0f", sp);
    return ESP_OK;
}

void autotune_cancel(const char *reason)
{
    if (!s_mtx) return;
    lock();
    if (running_locked()) {
        s.cancel_req = true;
        snprintf(s.cancel_reason, sizeof s.cancel_reason, "%s", reason ? reason : "CANCELADO");
    }
    unlock();
}

void autotune_ack(void)
{
    if (!s_mtx) return;
    lock();
    if (s.state == AUTOTUNE_DONE || s.state == AUTOTUNE_ABORTED) s.state = AUTOTUNE_IDLE;
    unlock();
}

void autotune_get_status(autotune_status_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s_mtx) return;
    lock();
    out->state     = s.state;
    out->sp        = s.sp;
    out->temp      = s.temp;
    out->duty      = running_locked() ? (s.relay_on ? s.bias + s.d : s.bias - s.d) : 0.0f;
    out->cycles    = s.n;
    out->elapsed_s = s.elapsed_s;
    out->kp        = s.kp;
    out->ki        = s.ki;
    out->kd        = s.kd;
    out->ku        = s.ku_avg;
    out->pu        = s.pu_avg;
    snprintf(out->reason, sizeof out->reason, "%s", s.reason);
    unlock();
}

bool autotune_is_running(void)
{
    if (!s_mtx) return false;
    lock();
    const bool r = running_locked();
    unlock();
    return r;
}

bool autotune_fans_on(void)
{
    if (!s_mtx) return false;
    lock();
    const bool r = running_locked() || s.cooling;
    unlock();
    return r;
}

float autotune_step(float temp, bool sensor_fault, bool session_active, float dt)
{
    if (!s_mtx) return 0.0f;
    char  msg[TELEM_EVENT_MSG_MAX] = "";
    float out = 0.0f;

    lock();
    s.temp = temp;
    if (running_locked()) {
        s.elapsed_s += dt;
        const char *why = NULL;
        if (s.cancel_req)                           why = s.cancel_reason;
        else if (session_active)                    why = "SE INICIO UN PROCESO";
        else if (sensor_fault)                      why = "FALLA DE SONDA";
        else if (temp > s.sp + AUTOTUNE_MAX_OVER_C) why = "SE PASO 10 C DEL SP";
        else if (s.elapsed_s > AUTOTUNE_TIMEOUT_S)  why = "TIEMPO AGOTADO";

        if (why) finish_locked(AUTOTUNE_ABORTED, why);
        else     out = relay_step_locked(temp);

        if (s.state == AUTOTUNE_DONE) {
            snprintf(msg, sizeof msg, "autotune OK Kp %.3g Ki %.3g Kd %.3g", s.kp, s.ki, s.kd);
        } else if (s.state == AUTOTUNE_ABORTED) {
            snprintf(msg, sizeof msg, "autotune cancelado: %.34s", s.reason);
        }
    }
    s.cancel_req = false;

    if (s.cooling) {
        s.cool_s += dt;
        if (session_active || (!sensor_fault && temp <= COOLDOWN_TEMP_C) ||
            s.cool_s >= (float)COOLDOWN_DURATION_S) {
            s.cooling = false;
        }
    }
    unlock();

    if (msg[0]) {
        ESP_LOGW(TAG, "%s", msg);
        telemetry_log_event(TELEM_EVT_SERVICE, "%s", msg);
    }
    return out;
}

void autotune_set_tuned(bool tuned) { s_tuned = tuned; }
bool autotune_is_tuned(void)        { return s_tuned; }
