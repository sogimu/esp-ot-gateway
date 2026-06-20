#include "infrastructure/driving/http_controller_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "infrastructure/driven/sntp_time_adapter.h"
#include "infrastructure/driving/web_page.h"
#include "infrastructure/driving/web_page_gz.inc"
#include "infrastructure/driving/json_helpers.h"
#include "application/ports/driving/iconfigure_system.h"
#include "application/ports/driving/iwifi_manager.h"
#include "domain/value_objects/ch_mode.h"
#include "application/ports/driving/iconfigure_pid.h"
#include "application/ports/driving/igas_calibration.h"
#include "application/ports/driving/ifault_reset.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <string>

static const char* TAG = "http";

HttpControllerAdapter* HttpControllerAdapter::s_self = nullptr;

HttpControllerAdapter::HttpControllerAdapter()  { s_self = this; }
HttpControllerAdapter::~HttpControllerAdapter() { stop(); s_self = nullptr; }

void HttpControllerAdapter::start()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers    = 29;
    config.lru_purge_enable    = true;
    config.stack_size          = 16384;   // +6KB for 90KB page + JSON serialisation
    config.recv_wait_timeout   = 10;      // prevent slow-client worker exhaustion

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска HTTP сервера");
        return;
    }
    static const httpd_uri_t routes[] = {
        { .uri = "/",                  .method = HTTP_GET,  .handler = handler_root,          .user_ctx = NULL },
        { .uri = "/api/status",        .method = HTTP_GET,  .handler = handler_status,        .user_ctx = NULL },
        { .uri = "/api/control",       .method = HTTP_POST, .handler = handler_control,       .user_ctx = NULL },
        { .uri = "/api/log",           .method = HTTP_GET,  .handler = handler_log,           .user_ctx = NULL },
        { .uri = "/api/stats",         .method = HTTP_GET,  .handler = handler_stats,         .user_ctx = NULL },
        { .uri = "/api/schedule",      .method = HTTP_GET,  .handler = handler_schedule,      .user_ctx = NULL },
        { .uri = "/api/schedule",      .method = HTTP_POST, .handler = handler_schedule,      .user_ctx = NULL },
        { .uri = "/api/pid_schedule",  .method = HTTP_GET,  .handler = handler_pid_schedule,  .user_ctx = NULL },
        { .uri = "/api/pid_schedule",  .method = HTTP_POST, .handler = handler_pid_schedule,  .user_ctx = NULL },
        { .uri = "/api/pid_quality",  .method = HTTP_GET,  .handler = handler_pid_quality,  .user_ctx = NULL },

        // WiFi API
        { .uri = "/api/wifi/status",   .method = HTTP_GET,  .handler = handler_wifi_status,   .user_ctx = NULL },
        { .uri = "/api/wifi/scan",     .method = HTTP_GET,  .handler = handler_wifi_scan,     .user_ctx = NULL },
        { .uri = "/api/wifi/settings", .method = HTTP_POST, .handler = handler_wifi_settings, .user_ctx = NULL },
        { .uri = "/api/wifi/forget",   .method = HTTP_POST, .handler = handler_wifi_forget,   .user_ctx = NULL },

        // Improv
        { .uri = "/prov",              .method = HTTP_GET,  .handler = handler_prov_get,      .user_ctx = NULL },
        { .uri = "/prov",              .method = HTTP_POST, .handler = handler_prov_post,     .user_ctx = NULL },

        // System time
        { .uri = "/api/system/time",   .method = HTTP_GET,  .handler = handler_system_time_get,  .user_ctx = NULL },
        { .uri = "/api/system/time",   .method = HTTP_POST, .handler = handler_system_time_post, .user_ctx = NULL },

        // Captive portal detection URLs
        { .uri = "/generate_204",              .method = HTTP_GET, .handler = handler_generate_204,     .user_ctx = NULL },
        { .uri = "/hotspot-detect.html",       .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        { .uri = "/ncsi.txt",                  .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        { .uri = "/connecttest.txt",           .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },
        { .uri = "/success.txt",               .method = HTTP_GET, .handler = handler_captive_redirect, .user_ctx = NULL },

        // Health check
        { .uri = "/api/ping",              .method = HTTP_GET, .handler = handler_ping,            .user_ctx = NULL },
    };
    int route_count = sizeof(routes) / sizeof(routes[0]);
    for (int i = 0; i < route_count; i++)
        httpd_register_uri_handler(server_, &routes[i]);
    ESP_LOGI(TAG, "HTTP сервер запущен на порту %d", config.server_port);
}
void HttpControllerAdapter::stop() { if (server_) { httpd_stop(server_); server_ = nullptr; } }

esp_err_t HttpControllerAdapter::handler_root(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char*)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

esp_err_t HttpControllerAdapter::handler_status(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[2048];
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
        v = json_get_int(body, "\"ch_mode\"");   if (v >= 0) self->cfg_->set_ch_mode(static_cast<CHMode>(v));
        v = json_get_int(body, "\"dhw_enable\""); if (v >= 0) self->cfg_->set_dhw_enable(v != 0);
        f = json_get_float(body, "\"ch_setpoint\"");
        if (f > -1e37f) { if (f < 20) f = 20; if (f > 80) f = 80; self->cfg_->set_ch_setpoint(f); }
        f = json_get_float(body, "\"dhw_setpoint\"");
        if (f > -1e37f) { if (f < 35) f = 35; if (f > 65) f = 65; self->cfg_->set_dhw_setpoint(f); }
        f = json_get_float(body, "\"dhw_hysteresis\""); if (f > -1e37f) self->cfg_->set_dhw_hysteresis(f);
        f = json_get_float(body, "\"tz_offset\"");
        if (f > -1e37f) { int tz = (int)f; if (tz >= -12 && tz <= 14) self->cfg_->set_timezone(tz); }
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
        if (kp > -1e37f || ki > -1e37f || kd > -1e37f || dt >= 0 ||
            sensor >= 0 || target > -1e37f || lockout >= 0) {
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
    if (self->cfg_) {
        v = json_get_int(body, "\"reset_mod_stats\""); if (v > 0) self->cfg_->reset_modulation_stats();
        v = json_get_int(body, "\"reset_cycle_stats\""); if (v > 0) self->cfg_->reset_cycle_stats();
        v = json_get_int(body, "\"reset_gas_stats\""); if (v > 0) self->cfg_->reset_gas_stats();
    }
    if (self->gas_) {
        v = json_get_int(body, "\"reset_corrections\""); if (v > 0) self->gas_->reset_corrections();
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
    httpd_resp_set_type(req, "application/json");
    const char* json = self->presenter_->log_json();
    return httpd_resp_sendstr(req, json ? json : "{\"count\":0,\"events\":[]}");
}

esp_err_t HttpControllerAdapter::handler_schedule(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }

    if (req->method == HTTP_POST) {
        char body[512] = {0};
        int recv_len = httpd_req_recv(req, body, sizeof(body)-1);
        if (recv_len <= 0) {
            httpd_resp_sendstr(req, "{\"ok\":false}");
            return ESP_FAIL;
        }
        int en = json_get_int(body, "\"enabled\"");
        if (en < 0) en = 1;
        CH_Schedule sched;
        sched.enabled = (en != 0);
        const char* p = strstr(body, "\"temps\"");
        if (p) {
            p = strchr(p, '[');
            if (p) {
                p++;
                for (int h = 0; h < 24; h++) {
                    while (*p == ' ' || *p == ',') p++;
                    float v = static_cast<float>(atof(p));
                    if (v < 20) v = 20;
                    if (v > 80) v = 80;
                    sched.temps[h] = v;
                    while (*p && *p != ',' && *p != ']') p++;
                    if (*p == ']') break;
                    if (*p == ',') p++;
                }
            }
        }
        if (self->cfg_) self->cfg_->set_schedule(sched);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }

    static char buf[512];
    self->presenter_->render_schedule(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_pid_schedule(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }

    if (req->method == HTTP_POST) {
        char body[512] = {0};
        int recv_len = httpd_req_recv(req, body, sizeof(body)-1);
        if (recv_len <= 0) {
            httpd_resp_sendstr(req, "{\"ok\":false}");
            return ESP_FAIL;
        }
        int en = json_get_int(body, "\"enabled\"");
        if (en < 0) en = 1;
        PID_Schedule sched;
        sched.enabled = (en != 0);
        const char* p = strstr(body, "\"temps\"");
        if (p) {
            p = strchr(p, '[');
            if (p) {
                p++;
                for (int h = 0; h < 24; h++) {
                    while (*p == ' ' || *p == ',') p++;
                    float v = static_cast<float>(atof(p));
                    if (v < 16) v = 16;
                    if (v > 28) v = 28;
                    sched.temps[h] = v;
                    while (*p && *p != ',' && *p != ']') p++;
                    if (*p == ']') break;
                    if (*p == ',') p++;
                }
            }
        }
        if (self->cfg_) self->cfg_->set_pid_schedule(sched);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    }

    static char buf[512];
    self->presenter_->render_pid_schedule(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_stats(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[6144];  // room for stats + 32 correction log entries
    self->presenter_->render_stats(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_pid_quality(httpd_req_t* req) {
    auto* self = s_self;
    if (!self || !self->presenter_) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }
    static char buf[512];
    self->presenter_->render_pid_quality(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

// ── WiFi Handlers ──────────────────────────────────────────

esp_err_t HttpControllerAdapter::handler_wifi_status(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    if (!w) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }

    static char buf[256];
    const char* mode_str = "first_boot";
    if (w->mode() == IWifiManager::Mode::STA) mode_str = "sta";
    else if (w->mode() == IWifiManager::Mode::AP) mode_str = "ap";

    snprintf(buf, sizeof(buf),
        "{\"mode\":\"%s\",\"ssid\":\"%s\",\"sta_ip\":\"%s\","
        "\"ap_ip\":\"%s\",\"rssi\":%d,\"is_first_boot\":%s}",
        mode_str, w->connected_ssid(), w->sta_ip(), w->ap_ip(),
        w->rssi(),
        w->mode() == IWifiManager::Mode::FIRST_BOOT ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

// ── Async WiFi scan with background task ──────────────────
// The scan requires stopping/restarting WiFi which disconnects the HTTP
// client.  We run the scan in a FreeRTOS task, cache the results, and
// let the client poll until they are ready.

static struct {
    enum State { IDLE, SCANNING, DONE } state = IDLE;
    WifiApRecord records[20];
    int count = 0;
} s_scan;

static void scan_task(void* arg) {
    auto* w = static_cast<IWifiManager*>(arg);
    ESP_LOGI("http", "scan_task: starting scan...");
    s_scan.count = w->scan_networks(s_scan.records, 20);
    ESP_LOGI("http", "scan_task: done, %d networks", s_scan.count);
    s_scan.state = decltype(s_scan)::DONE;
    vTaskDelete(nullptr);
}

esp_err_t HttpControllerAdapter::handler_wifi_scan(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    if (!w) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_FAIL;
    }

    // If results are ready — return them
    if (s_scan.state == decltype(s_scan)::DONE) {
        ESP_LOGI("http", "WiFi scan: returning %d cached results", s_scan.count);

        static char buf[4096];
        int pos = snprintf(buf, sizeof(buf), "{\"networks\":[");
        for (int i = 0; i < s_scan.count; i++) {
            if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
            char esc[68] = {};
            const char* s = s_scan.records[i].ssid;
            char* d = esc;
            while (*s && (d - esc) < 66) {
                if (*s == '"' || *s == '\\') *d++ = '\\';
                *d++ = *s++;
            }
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%u,\"channel\":%u}",
                esc, s_scan.records[i].rssi, s_scan.records[i].auth_mode,
                s_scan.records[i].channel);
        }
        snprintf(buf + pos, sizeof(buf) - pos, "],\"count\":%d}", s_scan.count);

        s_scan.state = decltype(s_scan)::IDLE;  // allow next scan

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, buf);
    }

    // If scan is in progress — tell client to wait
    if (s_scan.state == decltype(s_scan)::SCANNING) {
        ESP_LOGI("http", "WiFi scan: still in progress");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"scanning\":true}");
    }

    // Start scan in background task
    ESP_LOGI("http", "WiFi scan: launching background task");
    s_scan.state = decltype(s_scan)::SCANNING;
    s_scan.count = 0;

    BaseType_t ok = xTaskCreate(scan_task, "wifi_scan", 4096, w,
                                5, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE("http", "WiFi scan: failed to create task");
        s_scan.state = decltype(s_scan)::IDLE;
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"networks\":[],\"count\":0}");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"scanning\":true}");
}

esp_err_t HttpControllerAdapter::handler_wifi_settings(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    if (!w) { httpd_resp_sendstr(req, "{\"ok\":false}"); return ESP_FAIL; }

    char body[256] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
        return ESP_FAIL;
    }

    int mode = json_get_int(body, "\"mode\"");
    if (mode != 1 && mode != 2) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"invalid mode\"}");
        return ESP_FAIL;
    }

    int slen = 0, plen = 0, alen = 0;
    const char* ssid = json_get_string(body, "\"sta_ssid\"", slen);
    const char* pass = json_get_string(body, "\"sta_pass\"", plen);
    const char* ap   = json_get_string(body, "\"ap_pass\"", alen);

    char sta_ssid[33] = {}, sta_pass[65] = {}, ap_pass[65] = {};
    if (ssid) { int n = slen < 32 ? slen : 32; memcpy(sta_ssid, ssid, n); }
    if (pass) { int n = plen < 63 ? plen : 63; memcpy(sta_pass, pass, n); }
    if (ap)   { int n = alen < 63 ? alen : 63; memcpy(ap_pass, ap,   n); }

    // Send response BEFORE reboot
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    w->save_settings_and_reboot(mode, sta_ssid, sta_pass, ap_pass);
    return ESP_OK;
}

esp_err_t HttpControllerAdapter::handler_wifi_forget(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, w ? "{\"ok\":true}" : "{\"ok\":false}");
    if (w) w->factory_reset_and_reboot();
    return ESP_OK;
}

// ── Improv /prov ──────────────────────────────────────────

esp_err_t HttpControllerAdapter::handler_prov_get(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    static char buf[192];
    if (w && w->mode() == IWifiManager::Mode::FIRST_BOOT) {
        snprintf(buf, sizeof(buf),
            "{\"state\":\"provisioning\",\"service\":\"baxi-ot-gateway\","
            "\"name\":\"Baxi Duo-tec Compact\"}");
    } else if (w && w->mode() == IWifiManager::Mode::STA) {
        snprintf(buf, sizeof(buf),
            "{\"state\":\"provisioned\",\"ip\":\"%s\","
            "\"service\":\"baxi-ot-gateway\"}", w->sta_ip());
    } else {
        snprintf(buf, sizeof(buf),
            "{\"state\":\"provisioning\",\"service\":\"baxi-ot-gateway\"}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_prov_post(httpd_req_t* req) {
    auto* w = s_self ? s_self->wifi_ : nullptr;
    if (!w) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }

    char body[256] = {};
    httpd_req_recv(req, body, sizeof(body) - 1);

    int slen = 0, plen = 0;
    const char* ssid = json_get_string(body, "\"ssid\"", slen);
    const char* pass = json_get_string(body, "\"password\"", plen);

    if (!ssid || slen == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req,
            "{\"state\":\"provisioning\",\"error\":\"invalid_json\"}");
        return ESP_FAIL;
    }

    char sta_ssid[33] = {}, sta_pass[65] = {};
    memcpy(sta_ssid, ssid, slen < 32 ? slen : 32);
    if (pass) memcpy(sta_pass, pass, plen < 63 ? plen : 63);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"state\":\"provisioned\",\"service\":\"baxi-ot-gateway\"}");

    w->save_settings_and_reboot(1, sta_ssid, sta_pass, "");
    return ESP_OK;
}

// ── System Time ────────────────────────────────────────────

esp_err_t HttpControllerAdapter::handler_system_time_get(httpd_req_t* req) {
    auto* t = s_self ? s_self->time_ : nullptr;
    if (!t) { httpd_resp_sendstr(req, "{}"); return ESP_FAIL; }

    static char buf[128];
    time_t epoch = std::chrono::duration_cast<std::chrono::seconds>(
        t->now().time_since_epoch()).count();
    const char* src = t->is_synced() ? "sntp" : "manual";
    if (epoch < 1000000000) src = "none";  // pre-2001 = not set

    snprintf(buf, sizeof(buf),
        "{\"epoch\":%lld,\"source\":\"%s\",\"tz_offset\":%d}",
        (long long)epoch, src, t->tz_offset());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpControllerAdapter::handler_system_time_post(httpd_req_t* req) {
    auto* t = s_self ? s_self->time_ : nullptr;
    if (!t) { httpd_resp_sendstr(req, "{\"ok\":false}"); return ESP_FAIL; }

    char body[64] = {};
    httpd_req_recv(req, body, sizeof(body) - 1);

    float f = json_get_float(body, "\"epoch\"");
    if (f < 1000000000.0f || f > 4102444800.0f) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"invalid epoch\"}");
        return ESP_FAIL;
    }

    time_t epoch = (time_t)f;
    t->set_manual_time(epoch);
    t->save_time_offset();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// ── Android Internet Check (keep phone on AP) ──────────────

esp_err_t HttpControllerAdapter::handler_generate_204(httpd_req_t* req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

// ── Captive Portal Redirect ────────────────────────────────

esp_err_t HttpControllerAdapter::handler_ping(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t HttpControllerAdapter::handler_captive_redirect(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}
