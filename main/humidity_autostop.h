#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Auto-stop por humedad (modo automático).
//
// Si la sesión activa (manual o receta) tiene un objetivo de humedad (>0) y la
// humedad de cámara baja a ese valor de forma SOSTENIDA (después de un tiempo
// mínimo), completa el proceso igual que si se hubiera cumplido el tiempo:
// run_state = COMPLETED, apaga calefactor + fan, suena la alarma y loguea el
// evento (la nube pushea el cambio de estado = aviso). El tiempo configurado de
// la sesión queda como TOPE máximo de seguridad (lo que ocurra primero).
//
// Robustez: histéresis temporal (HUM_AUTOSTOP_SUSTAIN_S), tiempo mínimo inicial
// (HUM_AUTOSTOP_MIN_RUN_S) y, si el sensor de humedad falla, NO decide con datos
// malos — avisa una vez y deja que el tope de tiempo cierre el proceso.
//
// Se llama desde control_task cada ciclo, DESPUÉS de modo_manual_tick /
// programa_tick. Sin objetivo (=0) o sin sesión corriendo, es no-op.
void humidity_autostop_tick(float dt_s);

#ifdef __cplusplus
}
#endif
