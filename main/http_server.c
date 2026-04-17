#include "http_server.h"
#include "web_page.h"

#include "esp_log.h"
#include "esp_netif.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http";

/* Указатель на состояние котла (передаётся при старте сервера) */
static OT_State *g_state = NULL;

/* ── JSON helpers ──────────────────────────────────────────────────────────── */

static float json_get_float(const char *json, const char *key)
{
    const char *p = strstr(json, key);
    if (!p) return -1e38f;
    p += strlen(key);
    while (*p == ':' || *p == ' ') p++;
    return (float)atof(p);
}

static int json_get_int(const char *json, const char *key)
{
    float v = json_get_float(json, key);
    return (v < -1e37f) ? -1 : (int)v;
}

/* ── Handlers ──────────────────────────────────────────────────────────────── */

/* GET / → HTML страница */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/status → JSON */
static esp_err_t handler_status(httpd_req_t *req)
{
    OT_State *s = g_state;
    char timebuf[16];
    get_current_time_str(timebuf, sizeof(timebuf));
    char buf[1024];
    int  len = snprintf(buf, sizeof(buf),
        "{"
        "\"connected\":%d,"
        "\"fault\":%d,"
        "\"ch_active\":%d,"
        "\"dhw_active\":%d,"
        "\"flame\":%d,"
        "\"ch_temp\":%.1f,"
        "\"return_temp\":%.1f,"
        "\"dhw_temp\":%.1f,"
        "\"outside_temp\":%.1f,"
        "\"modulation\":%.1f,"
        "\"ch_setpoint\":%.1f,"
        "\"dhw_setpoint\":%.1f,"
        "\"dhw_sp_min\":%.0f,"
        "\"dhw_sp_max\":%.0f,"
        "\"ch_sp_min\":%.0f,"
        "\"ch_sp_max\":%.0f,"
        "\"ch_enable\":%d,"
        "\"dhw_enable\":%d,"
        "\"dhw_priority\":%d,"
        "\"sched_on\":%d,"
        "\"hour\":%d,"
        "\"time\":\"%s\","
        "\"asf_flags\":%d,"
        "\"oem_fault\":%d,"
        "\"oem_diag\":%d,"
        "\"slave_type\":%d,"
        "\"slave_ver\":%d,"
        "\"ot_ver\":%.1f,"
        "\"tz_offset\":%d"
        "}",
        s->connected    ? 1 : 0,
        s->fault        ? 1 : 0,
        s->ch_active    ? 1 : 0,
        s->dhw_active   ? 1 : 0,
        s->flame        ? 1 : 0,
        (double)s->ch_temp,
        (double)s->return_temp,
        (double)s->dhw_temp,
        (double)s->outside_temp,
        (double)s->modulation,
        (double)s->ch_setpoint,
        (double)s->dhw_setpoint,
        (double)s->dhw_setpoint_min,
        (double)s->dhw_setpoint_max,
        (double)s->ch_setpoint_min,
        (double)s->ch_setpoint_max,
        s->ch_enable      ? 1 : 0,
        s->dhw_enable     ? 1 : 0,
        s->dhw_priority   ? 1 : 0,
        g_schedule.enabled ? 1 : 0,
        get_current_hour(),
        timebuf,
        s->asf_flags,
        s->oem_fault_code,
        s->oem_diagnostic,
        s->slave_type,
        s->slave_version,
        (double)s->ot_version,
        g_tz_offset
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, len);
}

/* POST /api/control → изменить уставки */
static esp_err_t handler_control(httpd_req_t *req)
{
    char body[256] = {0};
    int  received  = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    OT_State *s = g_state;
    int   v;
    float f;

    v = json_get_int(body, "\"ch_enable\"");
    if (v >= 0) s->ch_enable = (v != 0);

    v = json_get_int(body, "\"dhw_enable\"");
    if (v >= 0) s->dhw_enable = (v != 0);

    f = json_get_float(body, "\"ch_setpoint\"");
    if (f > -1e37f) {
        if (f < 20.0f) f = 20.0f;
        if (f > 80.0f) f = 80.0f;
        s->ch_setpoint = f;
    }

    f = json_get_float(body, "\"dhw_setpoint\"");
    if (f > -1e37f) {
        if (f < 35.0f) f = 35.0f;
        if (f > 65.0f) f = 65.0f;
        s->dhw_setpoint = f;
    }

    v = json_get_int(body, "\"fault_reset\"");
    if (v > 0) s->fault_reset = true;

    v = json_get_int(body, "\"tz_offset\"");
    if (v > -100) { g_tz_offset = v; apply_tz(); }

    ESP_LOGI(TAG, "Control: CH=%d sp=%.0f  DHW=%d sp=%.0f",
             s->ch_enable, (double)s->ch_setpoint,
             s->dhw_enable, (double)s->dhw_setpoint);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* GET /api/schedule → JSON расписание */
static esp_err_t handler_schedule_get(httpd_req_t *req)
{
    char buf[512];
    int  len = snprintf(buf, sizeof(buf),
        "{\"enabled\":%d,\"hour\":%d,\"temps\":[",
        g_schedule.enabled ? 1 : 0,
        get_current_hour());
    for (int i = 0; i < 24; i++) {
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s%.0f", i ? "," : "", (double)g_schedule.temps[i]);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, len);
}

/* POST /api/schedule → сохранить расписание */
static esp_err_t handler_schedule_post(httpd_req_t *req)
{
    char body[512] = {0};
    int  received  = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    /* enabled */
    int v = json_get_int(body, "\"enabled\"");
    if (v >= 0) g_schedule.enabled = (v != 0);

    /* temps — парсим массив вручную */
    const char *p = strstr(body, "\"temps\"");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            for (int i = 0; i < 24 && *p; i++) {
                while (*p == ' ' || *p == ',') p++;
                if (*p == ']') break;
                float t = (float)atof(p);
                if (t >= 20.0f && t <= 80.0f) g_schedule.temps[i] = t;
                while (*p && *p != ',' && *p != ']') p++;
            }
        }
    }

    ESP_LOGI(TAG, "Schedule: enabled=%d", g_schedule.enabled);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ── Запуск сервера ─────────────────────────────────────────────────────────  */

httpd_handle_t HTTP_Server_Start(OT_State *state)
{
    g_state = state;

    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.server_port     = 80;
    config.max_uri_handlers = 8;
    config.stack_size      = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска HTTP сервера");
        return NULL;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = handler_root         },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = handler_status       },
        { .uri = "/api/control",   .method = HTTP_POST, .handler = handler_control      },
        { .uri = "/api/schedule",  .method = HTTP_GET,  .handler = handler_schedule_get },
        { .uri = "/api/schedule",  .method = HTTP_POST, .handler = handler_schedule_post},
    };

    for (int i = 0; i < 5; i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP сервер запущен на порту %d", config.server_port);
    return server;
}
