#pragma once

#include "model/model.h"
#include "endpoints/endpoints.h"
#include "interfaces/iopentherm_observer.h"
#include "interfaces/iwebserver_observer.h"
#include "interfaces/isensors_observer.h"
#include "log/log_service.h"

class Controller {
public:
    Controller(Model& model, Endpoints& endpoints, LogService& log_service);

    void start();

private:
    Model&     model_;
    Endpoints& endpoints_;
    LogService& log_service_;

    int last_schedule_hour_;

    class OpenthermObserver : public IOpenthermObserver {
        Controller& c_;
    public:
        explicit OpenthermObserver(Controller& c) : c_(c) {}
        void on_connected() override;
        void on_disconnected() override;
        void on_status_changed(bool fault, bool flame, bool ch_active, bool dhw_active) override;
        void on_ch_temp(float value) override;
        void on_dhw_temp(float value) override;
        void on_return_temp(float value) override;
        void on_outside_temp(float value) override;
        void on_modulation(float pct) override;
        void on_ch_bounds(float min, float max) override;
        void on_dhw_bounds(float min, float max) override;
        void on_fault_codes(uint8_t asf, uint8_t oem_fault, uint16_t oem_diag) override;
        void on_runtime_counters(uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs) override;
        void on_runtime_hours(uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh) override;
        void on_version(uint8_t st, uint8_t sv, float ov) override;
        void on_dhw_session_finished(uint32_t dur_ms, float min_temp) override;
    };

    class WebServerObserver : public IWebServerObserver {
        Controller& c_;
    public:
        explicit WebServerObserver(Controller& c) : c_(c) {}
        void on_cmd_set_ch_enable(bool enable) override;
        void on_cmd_set_dhw_enable(bool enable) override;
        void on_cmd_set_ch_setpoint(float temp) override;
        void on_cmd_set_dhw_setpoint(float temp) override;
        void on_cmd_fault_reset() override;
        void on_cmd_set_schedule(const CH_Schedule& schedule) override;
        void on_cmd_set_timezone(int offset) override;
        void on_cmd_set_k_calib(float value) override;
    };

    class SensorsObserver : public ISensorsObserver {
        Controller& c_;
    public:
        explicit SensorsObserver(Controller& c) : c_(c) {}
        void on_sensor_data(int sensor_id, float temperature) override;
    };

    OpenthermObserver  ot_obs_{*this};
    WebServerObserver  web_obs_{*this};
    SensorsObserver    sens_obs_{*this};

    void apply_schedule();
};