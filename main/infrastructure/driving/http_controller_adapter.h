#pragma once

#include "esp_http_server.h"
#include "application/ports/driving/iota_manager.h"

class WebPresenterAdapter;
class IConfigureSystem;
class IConfigurePid;
class IGasCalibration;
class IFaultReset;
class IWifiManager;
class SntpTimeAdapter;
class IMqttConfigurator;

class HttpControllerAdapter {
public:
    HttpControllerAdapter(WebPresenterAdapter& presenter,
                          IConfigureSystem& cfg, IConfigurePid& pid,
                          IGasCalibration& gas, IFaultReset& fault,
                          IWifiManager& wifi, SntpTimeAdapter& time,
                          IMqttConfigurator& mqtt);
    ~HttpControllerAdapter();
    bool start();
    void stop();

    void set_ota(IOtaManager* o) { ota_ = o; }  // после http.start()

private:
    httpd_handle_t server_ = nullptr;
    WebPresenterAdapter* presenter_ = nullptr;
    IConfigureSystem*    cfg_      = nullptr;
    IConfigurePid*       pid_      = nullptr;
    IGasCalibration*     gas_      = nullptr;
    IFaultReset*         fault_    = nullptr;
    IWifiManager*        wifi_     = nullptr;
    SntpTimeAdapter*     time_     = nullptr;
    IMqttConfigurator*   mqtt_     = nullptr;
    IOtaManager*         ota_      = nullptr;
    static HttpControllerAdapter* s_self;

    static esp_err_t handler_root(httpd_req_t* req);
    static esp_err_t handler_status(httpd_req_t* req);
    static esp_err_t handler_control(httpd_req_t* req);
    static esp_err_t handler_log(httpd_req_t* req);
    static esp_err_t handler_stats(httpd_req_t* req);
    static esp_err_t handler_schedule(httpd_req_t* req);
    static esp_err_t handler_pid_schedule(httpd_req_t* req);
    static esp_err_t handler_pid_quality(httpd_req_t* req);
    static esp_err_t handler_wifi_status(httpd_req_t* req);
    static esp_err_t handler_wifi_scan(httpd_req_t* req);
    static esp_err_t handler_wifi_settings(httpd_req_t* req);
    static esp_err_t handler_wifi_forget(httpd_req_t* req);
    static esp_err_t handler_prov_get(httpd_req_t* req);
    static esp_err_t handler_prov_post(httpd_req_t* req);
    static esp_err_t handler_system_time_get(httpd_req_t* req);
    static esp_err_t handler_system_time_post(httpd_req_t* req);
    static esp_err_t handler_captive_redirect(httpd_req_t* req);
    static esp_err_t handler_generate_204(httpd_req_t* req);
    static esp_err_t handler_ping(httpd_req_t* req);
    static esp_err_t handler_mqtt_status(httpd_req_t* req);
    static esp_err_t handler_mqtt_settings(httpd_req_t* req);
    static esp_err_t handler_ota_status(httpd_req_t* req);
    static esp_err_t handler_ota_versions(httpd_req_t* req);
    static esp_err_t handler_ota_start(httpd_req_t* req);
    static esp_err_t handler_ota_rollback(httpd_req_t* req);
    static esp_err_t handler_ota_upload(httpd_req_t* req);
};
