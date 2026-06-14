/// Tests for PidPollInteractor enable/disable lifecycle + PID schedule.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "application/use_cases/pid_poll_interactor.h"
#include "domain/value_objects/ch_schedule.h"
#include "application/ports/driven/ilogger.h"
#include "fakes/fake_heating_state_store.h"
#include "fakes/fake_boiler_hardware.h"
#include "fakes/fake_time_source.h"
#include <cstring>
#include <cstdarg>

using Catch::Approx;

struct PidPollTestLogger : public ILogger {
    void event(ILogger::Category, const char* fmt, ...) override {
        va_list args; va_start(args, fmt);
        vsnprintf(last_msg_, sizeof(last_msg_), fmt, args);
        va_end(args); event_count_++;
    }
    char last_msg_[256] = {};
    int event_count_ = 0;
};

// ═══ Constructor reads ch_mode from state ═══

TEST_CASE("PidPoll: конструктор читает PID_Static из state и включает PID", "[pid][lifecycle][config]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == true);
}
TEST_CASE("PidPoll: конструктор читает PID_Sched из state и включает PID", "[pid][lifecycle][config]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Sched);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == true);
}
TEST_CASE("PidPoll: конструктор читает Manual_Static из state и НЕ включает PID", "[pid][lifecycle][config]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::Manual_Static);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == false);
}
TEST_CASE("PidPoll: конструктор читает Manual_Sched из state и НЕ включает PID", "[pid][lifecycle][config]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::Manual_Sched);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == false);
}

// ═══ enable() / disable() set entity state ═══

TEST_CASE("PidPoll: enable() устанавливает pid_enabled=true в entity", "[pid][entity]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::Manual_Static);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == false);
    pid.enable();
    REQUIRE(state.get_pid_enabled() == true);
}
TEST_CASE("PidPoll: disable() устанавливает pid_enabled=false и сбрасывает active", "[pid][entity]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_enabled() == true);
    pid.disable();
    REQUIRE(state.get_pid_enabled() == false);
    REQUIRE(state.get_pid_active() == false);
}
TEST_CASE("PidPoll: enable→disable→enable цикл корректно обновляет entity", "[pid][entity]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::Manual_Static);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(!state.get_pid_enabled());
    pid.enable();  REQUIRE(state.get_pid_enabled());
    pid.disable(); REQUIRE(!state.get_pid_enabled());
    pid.enable();  REQUIRE(state.get_pid_enabled());
}

// ═══ poll() enabled / disabled ═══

TEST_CASE("PidPoll: poll() не делает ничего когда PID выключен", "[pid][poll][disabled]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::Manual_Static);
    state.set_t1_temp(22.0f); state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(61); pid.poll();
    REQUIRE(log.event_count_ == 0);
    REQUIRE(!state.get_pid_active());
}
TEST_CASE("PidPoll: poll() вычисляет PID когда включён и есть валидный датчик", "[pid][poll][enabled]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static);
    state.set_t1_temp(18.0f); state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll();
    time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_active()); REQUIRE(state.get_pid_output() >= 20.0f);
}

// ═══ disable() stops further polls ═══

TEST_CASE("PidPoll: disable() полностью останавливает — последующий poll() бездействует", "[pid][poll][disabled]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static);
    state.set_t1_temp(18.0f); state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_active());
    pid.disable();
    int before = log.event_count_;
    time.advance_sec(61); pid.poll();
    REQUIRE(log.event_count_ == before);
}

// ═══ re-enable starts clean ═══

TEST_CASE("PidPoll: enable после disable запускает PID с чистого состояния", "[pid][lifecycle]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static);
    state.set_t1_temp(18.0f); state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_active());
    pid.disable(); REQUIRE(!state.get_pid_enabled());
    pid.enable();  REQUIRE(state.get_pid_enabled());
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_active()); REQUIRE(state.get_pid_output() >= 20.0f);
}

// ═══ Config params ═══

TEST_CASE("PidPoll: target_room из конфига", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 25.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(25.0f));
}
TEST_CASE("PidPoll: Kp из конфига — больший Kp даёт больший выход", "[pid][config][params]") {
    FakeHeatingStateStore s1; FakeBoilerHardware b1; FakeTimeSource t1; PidPollTestLogger l1;
    s1.set_ch_mode(CHMode::PID_Static); s1.set_t1_temp(10.0f);
    s1.set_pid_config(4.0f, 0.0f, 0.0f, 60, 0, 22.0f, 300);
    s1.set_ch_sp_min(20.0f); s1.set_ch_sp_max(80.0f);
    PidPollInteractor p1(s1, b1, t1, l1);
    t1.advance_sec(1); p1.poll(); t1.advance_sec(60); p1.poll();
    float out_hi = s1.get_pid_output();
    FakeHeatingStateStore s2; FakeBoilerHardware b2; FakeTimeSource t2; PidPollTestLogger l2;
    s2.set_ch_mode(CHMode::PID_Static); s2.set_t1_temp(10.0f);
    s2.set_pid_config(1.0f, 0.0f, 0.0f, 60, 0, 22.0f, 300);
    s2.set_ch_sp_min(20.0f); s2.set_ch_sp_max(80.0f);
    PidPollInteractor p2(s2, b2, t2, l2);
    t2.advance_sec(1); p2.poll(); t2.advance_sec(60); p2.poll();
    REQUIRE(out_hi > s2.get_pid_output());
}
TEST_CASE("PidPoll: датчик T2 из конфига", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(18.0f); state.set_t2_temp(24.0f);
    state.set_pid_config(2.0f, 0.0f, 0.0f, 60, 1, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_room_temp() == Approx(24.0f));
}
TEST_CASE("PidPoll: dt из конфига", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(18.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 30, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(30); pid.poll();
    REQUIRE(state.get_pid_active());
}
TEST_CASE("PidPoll: lockout из конфига", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(18.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 120);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_lockout_sec() == 120);
}
TEST_CASE("PidPoll: выход PID ограничен ch_sp_min..ch_sp_max", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(10.0f);
    state.set_pid_config(10.0f, 0.1f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(30.0f); state.set_ch_sp_max(60.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    float out = state.get_pid_output();
    REQUIRE(out >= 30.0f); REQUIRE(out <= 60.0f);
}
TEST_CASE("PidPoll: hysteresis из конфига", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(22.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_pid_hysteresis(1.0f); state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_hysteresis() == Approx(1.0f));
}
TEST_CASE("PidPoll: Ki из конфига читается", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.05f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll(); time.advance_sec(60); pid.poll();
    REQUIRE(state.get_pid_ki() == Approx(0.05f));
}
TEST_CASE("PidPoll: Kd из конфига читается", "[pid][config][params]") {
    FakeHeatingStateStore state; FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.5f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_kd() == Approx(0.5f));
}

// ═══ PID_Sched: цель комнаты по расписанию ═══

TEST_CASE("PID_Sched: schedule updates target after first poll", "[pid][sched]") {
    FakeHeatingStateStore state;
    state.set_tz_offset(0); state.set_ch_mode(CHMode::PID_Sched); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);

    PID_Schedule ps; ps.enabled = true;
    for (int i = 0; i < 24; i++) ps.temps[i] = 22.0f;
    ps.temps[12] = 25.0f;
    state.set_pid_schedule(&ps);

    FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    time.set_us(12ULL * 3600 * 1000000);
    PidPollInteractor pid(state, boiler, time, log);
    REQUIRE(state.get_pid_target_room() == Approx(22.0f)); // до poll

    time.advance_sec(1); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(25.0f)); // из расписания
}

TEST_CASE("PID_Sched: ignored when mode is PID_Static", "[pid][sched]") {
    FakeHeatingStateStore state;
    state.set_tz_offset(0); state.set_ch_mode(CHMode::PID_Static); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);

    PID_Schedule ps; ps.enabled = true;
    for (int i = 0; i < 24; i++) ps.temps[i] = 22.0f;
    ps.temps[12] = 25.0f;
    state.set_pid_schedule(&ps);

    FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    time.set_us(12ULL * 3600 * 1000000);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(22.0f)); // не из расписания
}

TEST_CASE("PID_Sched: hour change updates target live", "[pid][sched]") {
    FakeHeatingStateStore state;
    state.set_tz_offset(0); state.set_ch_mode(CHMode::PID_Sched); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);

    PID_Schedule ps; ps.enabled = true;
    for (int i = 0; i < 24; i++) ps.temps[i] = 22.0f;
    ps.temps[8] = 24.0f; ps.temps[9] = 21.0f;
    state.set_pid_schedule(&ps);

    FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    time.set_us(8ULL * 3600 * 1000000);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(24.0f));

    time.set_us(9ULL * 3600 * 1000000);
    time.advance_sec(1); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(21.0f));
}

TEST_CASE("PID_Sched: disabled schedule keeps config target", "[pid][sched]") {
    FakeHeatingStateStore state;
    state.set_tz_offset(0); state.set_ch_mode(CHMode::PID_Sched); state.set_t1_temp(20.0f);
    state.set_pid_config(2.0f, 0.01f, 0.0f, 60, 0, 22.0f, 300);
    state.set_ch_sp_min(20.0f); state.set_ch_sp_max(80.0f);

    PID_Schedule ps; ps.enabled = false;
    for (int i = 0; i < 24; i++) ps.temps[i] = 22.0f;
    ps.temps[12] = 25.0f;
    state.set_pid_schedule(&ps);

    FakeBoilerHardware boiler; FakeTimeSource time; PidPollTestLogger log;
    time.set_us(12ULL * 3600 * 1000000);
    PidPollInteractor pid(state, boiler, time, log);
    time.advance_sec(1); pid.poll();
    REQUIRE(state.get_pid_target_room() == Approx(22.0f));
}
