#include "infrastructure/driving/http_controller_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "infrastructure/driving/web_page.h"
#include "application/ports/driving/iconfigure_system.h"
#include "application/ports/driving/iconfigure_pid.h"
#include "application/ports/driving/igas_calibration.h"
#include "application/ports/driving/ifault_reset.h"

#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static const char* TAG = "http";

HttpControllerAdapter* HttpControllerAdapter::s_self = nullptr;

HttpControllerAdapter::HttpControllerAdapter()  { s_self = this; }
HttpControllerAdapter::~HttpControllerAdapter() { stop(); s_self = nullptr; }

void HttpControllerAdapter::start()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 8;
    config.lru_purge_enable    = true;
    config.stack_size          = 10240; // need 10KB: handler buf (4K) + httpd internal (~3K) + render_status args

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска HTTP сервера");
        return;
    }
    static const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = handler_root,    .user_ctx = NULL },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = handler_status,  .user_ctx = NULL },
        { .uri = "/api/control",   .method = HTTP_POST, .handler = handler_control, .user_ctx = NULL },
        { .uri = "/api/log",       .method = HTTP_GET,  .handler = handler_log,     .user_ctx = NULL },
        { .uri = "/api/stats",     .method = HTTP_GET,  .handler = handler_stats,   .user_ctx = NULL },
        { .uri = "/api/schedule", .method = HTTP_GET,  .handler = handler_schedule, .user_ctx = NULL },
    };
    for (int i = 0; i < 6; i++)
        httpd_register_uri_handler(server_, &routes[i]);
    ESP_LOGI(TAG, "HTTP сервер запущен на порту %d", config.server_port);
}
void HttpControllerAdapter::stop() { if (server_) { httpd_stop(server_); server_ = nullptr; } }

static float json_get_float(const char* json, const char* key) {
    const char* p = strstr(json, key); if (!p) return -1e38f;
    p += strlen(key); while (*p == ':' || *p == ' ') p++; return (float)atof(p);
}
static int json_get_int(const char* json, const char* key) {
    float v = json_get_float(json, key); return (v < -1e37f) ? -1 : (int)v;
}

esp_err_t HttpControllerAdapter::handler_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpControllerAdapter::handler_status(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[2048];  // static: off stack, single-threaded (httpd serializes handlers)
    self->presenter_->render_status(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_control(httpd_req_t* req) {
    auto* self = s_self;
    if (!self) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    char body[256] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body)-1);
    if (recv_len <= 0) {
        ESP_LOGW(TAG, "POST пустой или ошибка: %d", recv_len);
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_FAIL;
    }
    int v; float f;
    if (self->cfg_) {
        v = json_get_int(body, "\"ch_enable\""); if (v >= 0) self->cfg_->set_ch_enable(v != 0);
        v = json_get_int(body, "\"ch_mode\"");   if (v >= 0) self->cfg_->set_ch_mode(v);
        v = json_get_int(body, "\"dhw_enable\""); if (v >= 0) self->cfg_->set_dhw_enable(v != 0);
        f = json_get_float(body, "\"ch_setpoint\"");
        if (f > -1e37f) { if (f < 20) f = 20; if (f > 80) f = 80; self->cfg_->set_ch_setpoint(f); }
        f = json_get_float(body, "\"dhw_setpoint\"");
        if (f > -1e37f) { if (f < 35) f = 35; if (f > 65) f = 65; self->cfg_->set_dhw_setpoint(f); }
        f = json_get_float(body, "\"dhw_hysteresis\""); if (f > -1e37f) self->cfg_->set_dhw_hysteresis(f);
        v = json_get_int(body, "\"tz_offset\""); if (v > -100) self->cfg_->set_timezone(v);
    }
    if (self->pid_) {
        float kp = json_get_float(body, "\"pid_kp\"");
        float ki = json_get_float(body, "\"pid_ki\"");
        float kd = json_get_float(body, "\"pid_kd\"");
        int dt = json_get_int(body, "\"pid_dt_sec\"");
        int sensor = json_get_int(body, "\"pid_room_sensor\"");
        float target = json_get_float(body, "\"pid_target_room\"");
        int lockout = json_get_int(body, "\"pid_cycle_lockout\"");
        float hyst = json_get_float(body, "\"pid_hysteresis\"");
        // If any PID param is present, send full parameter set
        if (kp > -1e37f || ki > -1e37f || kd > -1e37f || dt >= 0 ||
            sensor >= 0 || target > -1e37f || lockout >= 0) {
            // Fill missing with defaults from current state (read via presenter)
            if (kp <= -1e37f) kp = 2.0f;
            if (ki <= -1e37f) ki = 0.01f;
            if (kd <= -1e37f) kd = 0.0f;
            if (dt < 0) dt = 60;
            if (sensor < 0) sensor = 0;
            if (target <= -1e37f) target = 22.0f;
            if (lockout < 0) lockout = 300;
            self->pid_->set_pid_parameters(kp, ki, kd, dt, sensor, target, lockout);
        }
        if (hyst > -1e37f) self->pid_->set_pid_hysteresis(hyst);
    }
    if (self->fault_) { v = json_get_int(body, "\"fault_reset\""); if (v > 0) self->fault_->reset(); }
    if (self->gas_) {
        f = json_get_float(body, "\"k_calib\""); if (f > -1e37f) self->gas_->set_k_calib(f);
        f = json_get_float(body, "\"gas_meter_base\""); if (f > -1e37f) self->gas_->set_gas_meter_base(f);
        f = json_get_float(body, "\"gas_meter_correct\""); if (f > -1e37f) self->gas_->add_meter_correction(f);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t HttpControllerAdapter::handler_log(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{\"count\":0,\"events\":[]}"); return ESP_FAIL; }
    static char buf[4096];  // static: off stack, single-threaded (httpd serializes handlers)
    self->presenter_->render_log(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_schedule(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[512];  // static: off stack
    self->presenter_->render_schedule(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_stats(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[2048];  // static: off stack
    self->presenter_->render_stats(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}
