# Tasks: fix-ch-heating-control

## 1. Setpoint bounds — single source of truth

- [x] 1.1 Add private `clamp_ch(float)` / `clamp_dhw(float)` helpers to `SystemConfigInteractor` that read `get_ch_sp_min/max` and `get_dhw_sp_min/max` under `lock_shared()` and clamp; use fallback constants (CH `[25,80]`, DHW `[35,60]`) when bounds are invalid
- [x] 1.2 Rewrite `set_ch_setpoint` / `set_dhw_setpoint` to use the helpers (remove hardcoded `[20,80]` / `[35,80]`)
- [x] 1.3 Clamp schedule entries in `set_schedule` via the same helpers
- [x] 1.4 Remove hardcoded clamps in `http_controller_adapter.cpp` `handler_control` so the setter is authoritative
- [x] 1.5 Remove hardcoded `[20,80]` clamp in `handler_schedule`
- [x] 1.6 Remove hardcoded clamps in `mqtt_interactor.cpp` `handle_control`

## 2. CH setpoint re-clamp on write

- [x] 2.1 In `boiler_poll_interactor.cpp` `do_extra_step` (steps 1/10), re-clamp the CH setpoint to `get_ch_sp_max()` before writing ID 1, mirroring the DHW path

## 3. Tests

- [x] 3.1 Add `test/test_setpoint_bounds.cpp` (CH/DHW clamping, fallback, schedule)
- [x] 3.2 Update `test_mqtt_interactor.cpp` to assert pass-through (clamping is the setter's responsibility)

## 4. Docs & verification

- [x] 4.1 Update `docs/api.md` and `docs/api.en.md` (bounds clamping behavior)
- [x] 4.2 Run `./test/run_tests.sh`
- [x] 4.3 Build firmware per `docs/build.md`
