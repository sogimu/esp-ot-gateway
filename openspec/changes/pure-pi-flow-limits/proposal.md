## Why

Сейчас уставка PID ограничена только физическим максимумом котла (ID 49, обычно 80 °C). Для тёплого пола это неверно: нужны 27–30 °C, изредка до 40 °C. Интеграл разгоняет уставку до 63 °C и не может опуститься, потому что вкл/выкл управляется отдельным гистерезисом по комнате. Нужен чистый PI с настраиваемыми границами подачи.

## What Changes

- **Границы подачи в адаптивном режиме**: `pid_sp_min` и `pid_sp_max` (настраиваются, хранятся в NVS) ограничивают выход PI; физические границы котла (ID 49) остаются предохранителем.
- **Чистый PI без комнатного гистерезиса**: выход PI — уставка подачи в `[pid_sp_min, pid_sp_max]`.
- **Автоматический вкл/выкл CH по выходу PI**: CH включается, когда выход выше `pid_sp_min + deadband`, и выключается, когда выход упирается в `pid_sp_min`. `pid_hysteresis` теперь означает этот deadband по выходу, а не полосу по комнате.
- Интеграл стартует с `pid_sp_min` (нет мёртвой зоны снизу) и свободно уменьшается, когда комнате жарко.
- UI: поля «Мин. подача» и «Макс. подача» для адаптивного режима.

## Capabilities

### New Capabilities

- `pid-control`: чистый PI с настраиваемыми границами подачи и вкл/выкл по выходу.

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/pid_poll_interactor.{h,cpp}` — логика PI и вкл/выкл.
- `main/application/ports/driven/iheating_state_store.h`, `main/infrastructure/driven/heating_state_adapter.{h,cpp}` — `pid_sp_min/pid_sp_max`.
- `main/infrastructure/driven/boiler_nvs_store.cpp` — персист границ.
- `main/application/ports/driving/iconfigure_pid.h`, `main/application/use_cases/system_config_interactor.{h,cpp}`, `main/infrastructure/driving/http_controller_adapter.cpp` — настройка границ через API.
- `main/infrastructure/driven/web_presenter_adapter.cpp`, `main/infrastructure/driving/web_page.h` — отображение и поля.
- `test/fakes/fake_heating_state_store.h`, `test/test_pid_poll_interactor.cpp` — тесты.
- `README.md` / `README.en.md` — описание.
