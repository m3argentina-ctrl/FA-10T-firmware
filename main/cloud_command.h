#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMD_ID_MAX    32
#define CMD_TYPE_MAX  20
#define CMD_MAX       5

typedef struct {
    char     id[CMD_ID_MAX];
    char     type[CMD_TYPE_MAX];
    float    sp;
    uint32_t dur_s;
    float    hum;
    int8_t   slot;      // programa slot (0..5) para start_program; -1 = no aplica
} cloud_cmd_t;

// Parsea el body de respuesta del ingest buscando "cmds":[...].
// Llena cmds[] y devuelve la cantidad encontrada (0 si no hay).
int cloud_cmd_parse(const char *json, cloud_cmd_t *cmds, int max_cmds);

/**
 * Intervalo de heartbeat que pide el servidor en la respuesta ("next_push_s").
 * Permite que la nube afloje el ritmo cuando el equipo está en reposo y lo
 * acelere durante un proceso. Devuelve `def` si la respuesta no lo trae.
 */
uint32_t cloud_cmd_next_push_s(const char *json, uint32_t def);

// Ejecuta un comando despachando a modo_manual / programa.
// Devuelve true si se ejecutó con éxito.
bool cloud_cmd_execute(const cloud_cmd_t *cmd);

#ifdef __cplusplus
}
#endif
