#include "webserver_endpoint.h"
#include "web_page.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char* TAG = "http";

WebServerEndpoint* WebServerEndpoint::s_self = nullptr;

WebServerEndpoint::WebServerEndpoint()
    : model_(nullptr), server_(nullptr)
{
    s_self = this;
}

WebServerEndpoint::~WebServerEndpoint()
{
    stop();
    if (s_self == this) s_self = nullptr;
}

void WebServerEndpoint::start()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port         = 80;
    config.max_uri_handlers    = 8;
    config.stack_size          = 16384;
    config.lru_purge_enable    = true;
    config.recv_wait_timeout   = 3000;
    config.send_wait_timeout   = 3000;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска HTTP сервера");
        return;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = handler_root,         .user_ctx = NULL },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = handler_status,       .user_ctx = NULL },
        { .uri = "/api/control",   .method = HTTP_POST, .handler = handler_control,      .user_ctx = NULL },
        { .uri = "/api/schedule",  .method = HTTP_GET,  .handler = handler_schedule_get, .user_ctx = NULL },
        { .uri = "/api/schedule",  .method = HTTP_POST, .handler = handler_schedule_post,.user_ctx = NULL },
        { .uri = "/api/log",       .method = HTTP_GET,  .handler = handler_log,          .user_ctx = NULL },
        { .uri = "/api/stats",     .method = HTTP_GET,  .handler = handler_stats,        .user_ctx = NULL },
    };

    for (int i = 0; i < 7; i++)
        httpd_register_uri_handler(server_, &routes[i]);

    ESP_LOGI(TAG, "HTTP сервер запущен на порту %d", config.server_port);
}

void WebServerEndpoint::stop()
{
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

void WebServerEndpoint::set_model(const Model* model)
{
    model_ = model;
}

void WebServerEndpoint::subscribe(IWebServerObserver* obs)
{
    for (auto* o : observers_) {
        if (o == obs) return;
    }
    observers_.push_back(obs);
}

void WebServerEndpoint::unsubscribe(IWebServerObserver* obs)
{
    for (auto it = observers_.begin(); it != observers_.end(); ++it) {
        if (*it == obs) { observers_.erase(it); return; }
    }
}

void WebServerEndpoint::notify_cmd_ch_enable(bool enable)
{
    for (auto* o : observers_) o->on_cmd_set_ch_enable(enable);
}

void WebServerEndpoint::notify_cmd_dhw_enable(bool enable)
{
    for (auto* o : observers_) o->on_cmd_set_dhw_enable(enable);
}

void WebServerEndpoint::notify_cmd_ch_setpoint(float temp)
{
    for (auto* o : observers_) o->on_cmd_set_ch_setpoint(temp);
}

void WebServerEndpoint::notify_cmd_dhw_setpoint(float temp)
{
    for (auto* o : observers_) o->on_cmd_set_dhw_setpoint(temp);
}

void WebServerEndpoint::notify_cmd_fault_reset()
{
    for (auto* o : observers_) o->on_cmd_fault_reset();
}

void WebServerEndpoint::notify_cmd_set_schedule(const CH_Schedule& sched)
{
    for (auto* o : observers_) o->on_cmd_set_schedule(sched);
}

void WebServerEndpoint::notify_cmd_set_timezone(int offset)
{
    for (auto* o : observers_) o->on_cmd_set_timezone(offset);
}

float WebServerEndpoint::json_get_float(const char* json, const char* key)
{
    const char* p = strstr(json, key);
    if (!p) return -1e38f;
    p += strlen(key);
    while (*p == ':' || *p == ' ') p++;
    return (float)atof(p);
}

int WebServerEndpoint::json_get_int(const char* json, const char* key)
{
    float v = json_get_float(json, key);
    return (v < -1e37f) ? -1 : (int)v;
}

esp_err_t WebServerEndpoint::handler_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServerEndpoint::handler_status(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self || !self->model_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No model");
        return ESP_FAIL;
    }

    std::string json = self->model_->to_json();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json.c_str(), json.length());
}

esp_err_t WebServerEndpoint::handler_control(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No self");
        return ESP_FAIL;
    }

    char body[256] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    int   v;
    float f;

    v = json_get_int(body, "\"ch_enable\"");
    if (v >= 0) self->notify_cmd_ch_enable(v != 0);

    v = json_get_int(body, "\"dhw_enable\"");
    if (v >= 0) self->notify_cmd_dhw_enable(v != 0);

    f = json_get_float(body, "\"ch_setpoint\"");
    if (f > -1e37f) {
        if (f < 20.0f) f = 20.0f;
        if (f > 80.0f) f = 80.0f;
        self->notify_cmd_ch_setpoint(f);
    }

    f = json_get_float(body, "\"dhw_setpoint\"");
    if (f > -1e37f) {
        if (f < 35.0f) f = 35.0f;
        if (f > 65.0f) f = 65.0f;
        self->notify_cmd_dhw_setpoint(f);
    }

    v = json_get_int(body, "\"fault_reset\"");
    if (v > 0) self->notify_cmd_fault_reset();

    v = json_get_int(body, "\"tz_offset\"");
    if (v > -100) self->notify_cmd_set_timezone(v);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t WebServerEndpoint::handler_schedule_get(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self || !self->model_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No model");
        return ESP_FAIL;
    }

    const auto& sched = self->model_->get_schedule();
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "{\"enabled\":%d,\"hour\":%d,\"temps\":[",
        sched.enabled ? 1 : 0,
        0);
    for (int i = 0; i < 24; i++) {
        len += snprintf(buf + len, sizeof(buf) - len,
            "%s%.0f", i ? "," : "", (double)sched.temps[i]);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, len);
}

esp_err_t WebServerEndpoint::handler_schedule_post(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No self");
        return ESP_FAIL;
    }

    char body[512] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    CH_Schedule sched;
    sched.enabled = false;
    for (int i = 0; i < 24; i++) sched.temps[i] = 30.0f;

    int v = json_get_int(body, "\"enabled\"");
    if (v >= 0) sched.enabled = (v != 0);

    const char* p = strstr(body, "\"temps\"");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            for (int i = 0; i < 24 && *p; i++) {
                while (*p == ' ' || *p == ',') p++;
                if (*p == ']') break;
                float t = (float)atof(p);
                if (t >= 20.0f && t <= 80.0f) sched.temps[i] = t;
                while (*p && *p != ',' && *p != ']') p++;
            }
        }
    }

    self->notify_cmd_set_schedule(sched);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t WebServerEndpoint::handler_log(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self || !self->model_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No model");
        return ESP_FAIL;
    }
    std::string json = self->model_->to_log_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json.c_str(), json.length());
}

esp_err_t WebServerEndpoint::handler_stats(httpd_req_t* req)
{
    auto* self = s_self;
    if (!self || !self->model_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No model");
        return ESP_FAIL;
    }
    std::string json = self->model_->to_stats_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json.c_str(), json.length());
}