# Tasks: fix-ch-heating-control

## 1. Setpoint bounds — single source of truth

- [x] 1.1 Add private `clamp_ch(float)` / `clamp_dhw(float)` helpers to `SystemConfigInteractor` that read `get_ch_sp_min/max` and `get_dhw_sp_min/max` under `lock_shared()` and clamp; use fallback constants (CH `[25,80]`, DHW `[35,60]`) when bounds are invalid (`max <= min`)
- [x] 1.2 Rewrite `set_ch_setpoint` / `set_dhw_setpoint` to use the helpers (remove hardcoded `[20,80]` / `[35,80]`)
- [x] 1.3 Clamp schedule entries in `set_schedule` (and `set_pid_schedule` where it carries CH flow targets) via the same helpers
- [x] 1.4 Remove hardcoded clamps in `http_controller_adapter.cpp` `handler_control` (CH `[20,80]`, DHW `[35,65]`) so the setter is authoritative
- [x] 1.5 Remove hardcoded `[20,80]` clamp in `handler_schedule`; keep PID-schedule room-target clamp `[16,28]` unchanged
- [x] 1.6 Verify MQTT path does not write setpoints bypassing the setter (only `ch_enable` today); if it does, route through the setter

## 2. CH hysteresis

- [x] 2.1 Add `CHHysteresisService` (`main/application/services/ch_hysteresis_service.{h,cpp}`) with pure `should_heat(flow, setpoint, hyst, enabled)` / `should_stop(flow, setpoint)`, mirroring `DHWHysteresisService`
- [x] 2.2 Add unit test `test/test_ch_hysteresis.cpp` mirroring `test_dhw_hysteresis.cpp` (thresholds, dead band, disabled, edge cases)
- [x] 2.3 Add `ch_hysteresis` to `IHeatingStateStore` (`set_ch_hysteresis`/`get_ch_hysteresis`) and `HeatingStateAdapter`
- [x] 2.4 Persist `ch_hysteresis` in `boiler_nvs_store.cpp` (save/load, default 0 = disabled)
- [x] 2.5 Add `set_ch_hysteresis` to `IConfigureSystem` and `SystemConfigInteractor` (clamp 0..10 °C, log)
- [x] 2.6 Parse `ch_hysteresis` in `handler_control` and call the setter
- [x] 2.7 Add `do_ch_hysteresis()` and a local `ch_hysteresis_on_` flag to `BoilerPollInteractor`; call it each `execute()` cycle (like `do_dhw_hysteresis()`)
- [x] 2.8 Update `build_master_byte()`: `ch_on = ch_enable && (ch_hysteresis == 0 || ch_hysteresis_on_)`
- [x] 2.9 Expose `ch_hysteresis` in `web_presenter_adapter.cpp` status JSON and add a control input in `web_page.h`

## 3. Master Configuration (ID 2)

- [x] 3.1 Revert `do_handshake()` in `boiler_poll_interactor.cpp` to echo ID 3 into ID 2 (Baxi workaround: без эха котёл не разрешает DHW-управление)
- [x] 3.2 Revert legacy `ot_handshake()` in `c_legacy/opentherm.c` to echo ID 3 into ID 2

## 4. Docs & verification

- [x] 4.1 Update `docs/api.md` and `docs/api.en.md`: `ch_hysteresis` field, bounds clamping behavior, ID 2 note
- [x] 4.2 Run `./test/run_tests.sh` (all host tests, including new `test_ch_hysteresis.cpp`)
- [x] 4.3 Build firmware per `docs/build.md` to confirm no compile regressions
