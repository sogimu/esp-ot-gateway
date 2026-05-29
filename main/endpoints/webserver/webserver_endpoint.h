#pragma once

#include <vector>
#include "esp_http_server.h"
#include "model/model.h"
#include "interfaces/iwebserver_observer.h"

class WebServerEndpoint {
public:
    WebServerEndpoint();
    ~WebServerEndpoint();

    void start();
    void stop();

    void set_model(const Model* model);

    void subscribe(IWebServerObserver* obs);
    void unsubscribe(IWebServerObserver* obs);

private:
    const Model*                     model_;
    httpd_handle_t                   server_;
    std::vector<IWebServerObserver*> observers_;

    void notify_cmd_ch_enable(bool enable);
    void notify_cmd_dhw_enable(bool enable);
    void notify_cmd_ch_setpoint(float temp);
    void notify_cmd_dhw_setpoint(float temp);
    void notify_cmd_fault_reset();
    void notify_cmd_set_schedule(const CH_Schedule& sched);
    void notify_cmd_set_timezone(int offset);
    void notify_cmd_set_k_calib(float value);
    void notify_cmd_set_gas_meter_base(float value);
    void notify_cmd_add_gas_meter_correction(float reading);

    void notify_cmd_reset_modulation_stats();
    void notify_cmd_reset_cycle_stats();
    void notify_cmd_reset_gas_stats();

    static float json_get_float(const char* json, const char* key);
    static int   json_get_int(const char* json, const char* key);

    static esp_err_t handler_root(httpd_req_t* req);
    static esp_err_t handler_status(httpd_req_t* req);
    static esp_err_t handler_control(httpd_req_t* req);
    static esp_err_t handler_schedule_get(httpd_req_t* req);
    static esp_err_t handler_schedule_post(httpd_req_t* req);
    static esp_err_t handler_log(httpd_req_t* req);
    static esp_err_t handler_stats(httpd_req_t* req);

    static WebServerEndpoint* s_self;

    friend class Controller;
};