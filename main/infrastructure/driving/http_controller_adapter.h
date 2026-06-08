#pragma once

#include "esp_http_server.h"

class WebPresenterAdapter;
class IConfigureSystem;
class IConfigurePid;
class IGasCalibration;
class IFaultReset;

class HttpControllerAdapter {
public:
    HttpControllerAdapter();
    ~HttpControllerAdapter();
    void start();
    void stop();

    void set_presenter(WebPresenterAdapter* p)  { presenter_ = p; }
    void set_config(IConfigureSystem* c)        { cfg_ = c; }
    void set_pid(IConfigurePid* p)              { pid_ = p; }
    void set_gas(IGasCalibration* g)            { gas_ = g; }
    void set_fault(IFaultReset* f)              { fault_ = f; }

private:
    httpd_handle_t server_ = nullptr;
    WebPresenterAdapter* presenter_ = nullptr;
    IConfigureSystem*    cfg_      = nullptr;
    IConfigurePid*       pid_      = nullptr;
    IGasCalibration*     gas_      = nullptr;
    IFaultReset*         fault_    = nullptr;
    static HttpControllerAdapter* s_self;

    static esp_err_t handler_root(httpd_req_t* req);
    static esp_err_t handler_status(httpd_req_t* req);
    static esp_err_t handler_control(httpd_req_t* req);
    static esp_err_t handler_log(httpd_req_t* req);
    static esp_err_t handler_stats(httpd_req_t* req);
    static esp_err_t handler_schedule(httpd_req_t* req);
};
