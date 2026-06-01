#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opentherm.h"
#include "interfaces/iopentherm_observer.h"

class OpenthermEndpoint {
public:
    OpenthermEndpoint();
    ~OpenthermEndpoint();

    void start();
    void stop();

    void set_ch_enable(bool enable);
    void set_dhw_enable(bool enable);
    void set_ch_setpoint(float temp);
    void set_dhw_setpoint(float temp);
    void set_dhw_hysteresis(float value);
    void trigger_fault_reset();

    float get_ch_setpoint() const { return ot_.ch_setpoint; }
    float get_dhw_setpoint() const { return ot_.dhw_setpoint; }

    void subscribe(IOpenthermObserver* obs);
    void unsubscribe(IOpenthermObserver* obs);

private:
    static void task_wrapper(void* arg);
    void task_loop();
    void poll_cycle();

    void do_handshake();
    void do_dhw_hysteresis();
    void do_status();
    void do_extra_step();
    void check_connectivity();

    void notify_all();
    void notify_status(bool fault, bool flame, bool ch_active, bool dhw_active);

    uint8_t build_master_byte();

    OT_State    ot_;

    TaskHandle_t task_;
    bool         running_;

    bool   pending_ch_enable_;
    bool   pending_ch_enable_val_;
    bool   pending_dhw_enable_;
    bool   pending_dhw_enable_val_;
    bool   pending_fault_reset_;
    float  pending_ch_sp_;
    bool   pending_ch_sp_dirty_;
    float  pending_dhw_sp_;
    bool   pending_dhw_sp_dirty_;

    int         poll_step_;
    bool        handshake_done_;
    uint32_t    last_handshake_ms_;
    uint32_t    last_response_ms_;
    bool        connected_;

    bool   dhw_priority_;
    uint32_t dhw_session_start_ms_;
    float  dhw_session_min_temp_;

    bool   last_fault_, last_flame_, last_ch_active_, last_dhw_active_;

    std::vector<IOpenthermObserver*> observers_;
};
