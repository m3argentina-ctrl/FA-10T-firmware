#include "ota_update.h"

#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state.h"
#include "ssr3ch.h"
#include "ds18b20_bus.h"

static const char *TAG = "ota";

// Buffer de recepción. Estático a propósito: el stack del httpd es de 5 KB y no
// entra un buffer de 4 KB. Sólo se permite una actualización a la vez
// (s_in_progress), así que no hay reentrada.
static uint8_t s_buf[4096];
static bool    s_in_progress;

esp_err_t ota_update_mark_valid(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) return ESP_OK;

    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "imagen nueva en prueba (%s) — confirmando, no habrá rollback",
                 run->label);
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    ESP_LOGI(TAG, "corriendo desde '%s' (imagen ya confirmada)", run->label);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// GET /update — formulario de carga
// ---------------------------------------------------------------------------
static const char UPDATE_PAGE[] =
"<!doctype html><html lang=es><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Actualizar firmware</title><style>"
"body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:20px}"
"h1{color:#E87A20;font-size:20px}.c{max-width:520px;margin:auto}"
"input[type=file]{width:100%;padding:10px;background:#222;color:#eee;border:1px solid #444;border-radius:6px}"
"button{width:100%;padding:14px;margin-top:14px;background:#E87A20;color:#fff;border:0;"
"border-radius:6px;font-size:16px;font-weight:bold}button:disabled{background:#555}"
"p{color:#aaa;font-size:14px;line-height:1.5}.w{background:#3a2a00;border-left:4px solid #E8A020;padding:10px;border-radius:4px}"
"#s{margin-top:14px;font-size:15px}</style></head><body><div class=c>"
"<h1>Actualizar firmware</h1>"
"<div class=w><b>Antes de actualizar:</b> el equipo NO debe tener un proceso en marcha. "
"Al terminar se reinicia solo. No cortes la alimentacion durante la carga.</div>"
"<form id=f><p>Archivo <code>.bin</code> del firmware:</p>"
"<input type=file id=fw accept='.bin' required>"
"<button type=submit id=b>SUBIR Y ACTUALIZAR</button></form><div id=s></div>"
"<script>"
"const f=document.getElementById('f'),b=document.getElementById('b'),s=document.getElementById('s');"
"f.onsubmit=async e=>{e.preventDefault();const fl=document.getElementById('fw').files[0];"
"if(!fl)return;b.disabled=true;s.textContent='Subiendo '+(fl.size/1024|0)+' KB...';"
"try{const r=await fetch('/update',{method:'POST',body:fl});const t=await r.text();"
"s.textContent=t;if(r.ok){s.textContent='OK. Reiniciando... volve a cargar la pagina en ~20 s.';}"
"else{b.disabled=false;}}catch(err){s.textContent='Error de red: '+err;b.disabled=false;}};"
"</script></div></body></html>";

static esp_err_t update_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, UPDATE_PAGE, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// POST /update — recibe el .bin y lo graba en el slot inactivo
// ---------------------------------------------------------------------------
// Vacía lo que quede del cuerpo de la petición. Es IMPRESCINDIBLE antes de
// contestar un error: si el servidor responde y cierra mientras el navegador
// todavía está subiendo el .bin (1,8 MB), el browser ve la conexión cortada y
// muestra "Failed to fetch" en lugar del mensaje de error real.
static void drain_body(httpd_req_t *req)
{
    int guard = 0;
    while (guard++ < 4096) {          // tope defensivo (~16 MB)
        int n = httpd_req_recv(req, (char *)s_buf, sizeof(s_buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) break;            // 0 = no queda nada, <0 = error/cierre
    }
}

static esp_err_t fail(httpd_req_t *req, const char *msg)
{
    ESP_LOGE(TAG, "actualizacion abortada: %s", msg);
    s_in_progress = false;
    ds18b20_bus_set_paused(false);   // vuelve a leer sensores
    drain_body(req);                  // primero consumir, después responder
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, msg);
    // ESP_OK (no ESP_FAIL): devolver ESP_FAIL hace que httpd cierre el socket
    // de inmediato y el navegador nunca llega a leer la respuesta.
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    if (s_in_progress) return fail(req, "Ya hay una actualizacion en curso.");

    // SEGURIDAD: nunca actualizar con un proceso corriendo. La actualización
    // termina en reinicio, y reiniciar en mitad de un ciclo dejaría el
    // calefactor sin control durante el arranque.
    app_state_lock();
    const run_state_t rs = app_state_get()->run_state;
    app_state_unlock();
    if (rs == RUN_STATE_RUNNING || rs == RUN_STATE_PAUSED) {
        return fail(req, "Hay un proceso en marcha. Detenelo antes de actualizar.");
    }

    if (req->content_len <= 0) return fail(req, "Archivo vacio.");

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) return fail(req, "No hay particion OTA disponible (revisar partitions.csv).");
    if ((size_t)req->content_len > next->size) return fail(req, "El firmware no entra en la particion.");

    ESP_LOGI(TAG, "actualizacion iniciada: %d bytes -> '%s'",
             (int)req->content_len, next->label);
    s_in_progress = true;

    // Pausar el bus 1-Wire MIENTRAS se escribe flash: esp_ota_write() desactiva
    // la cache y bloquea interrupciones, y eso rompe el timing de microsegundos
    // del RMT que usa el 1-Wire -> las lecturas fallan y SAFETY dispara
    // SENSOR_FAULT en falso durante toda la actualizacion.
    ds18b20_bus_set_paused(true);

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(next, req->content_len, &h);
    if (err != ESP_OK) return fail(req, "esp_ota_begin fallo.");

    int remaining = req->content_len;
    bool header_ok = false;
    while (remaining > 0) {
        int n = httpd_req_recv(req, (char *)s_buf,
                               remaining < (int)sizeof(s_buf) ? remaining : (int)sizeof(s_buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;          // reintenta
        if (n <= 0) { esp_ota_abort(h); return fail(req, "Se corto la transferencia."); }

        // Verificación mínima del primer bloque: que sea una imagen de app del
        // ESP32 (magic 0xE9). Evita grabar un archivo cualquiera.
        if (!header_ok) {
            if (n < 1 || s_buf[0] != ESP_IMAGE_HEADER_MAGIC) {
                esp_ota_abort(h);
                return fail(req, "El archivo no es un firmware valido (.bin del proyecto).");
            }
            header_ok = true;
        }

        if (esp_ota_write(h, s_buf, n) != ESP_OK) {
            esp_ota_abort(h);
            return fail(req, "Error escribiendo en flash.");
        }
        remaining -= n;
    }

    err = esp_ota_end(h);   // valida checksum/firma de la imagen
    if (err != ESP_OK) {
        return fail(req, err == ESP_ERR_OTA_VALIDATE_FAILED
                    ? "La imagen esta corrupta (checksum invalido)."
                    : "esp_ota_end fallo.");
    }
    if (esp_ota_set_boot_partition(next) != ESP_OK) {
        return fail(req, "No se pudo marcar la particion de arranque.");
    }

    ESP_LOGW(TAG, "actualizacion OK — reiniciando en '%s'", next->label);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "OK");

    // Estado seguro antes del reset: salidas apagadas.
    ssr3ch_force_all_off();
    vTaskDelay(pdMS_TO_TICKS(1200));   // deja salir la respuesta HTTP
    esp_restart();
    return ESP_OK;                     // no se alcanza
}

esp_err_t ota_update_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    static const httpd_uri_t get_u = {
        .uri = "/update", .method = HTTP_GET,  .handler = update_get_handler };
    static const httpd_uri_t post_u = {
        .uri = "/update", .method = HTTP_POST, .handler = update_post_handler };

    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server, &get_u));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server, &post_u));

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "OTA por web lista en /update (slot destino: %s)",
             next ? next->label : "NINGUNO");
    return ESP_OK;
}
