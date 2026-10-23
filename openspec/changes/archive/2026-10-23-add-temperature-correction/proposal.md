## Why

Температуры DS18B20 (T1/T2) и котла (outside, CH supply, CH return, DHW) читаются напрямую из `IHeatingStateStore` и без какой-либо коррекции расходятся по всей логике (PID, газовая модель, DHW-гистерезис/прогноз, FOPDT, оценка качества PID, веб-интерфейс). Пользователь не может скомпенсировать систематическую ошибку датчика (например, датчик занижает на 2 °C). Из-за этого комнатная температура для PID и расчёт расхода газа используют смещённые показания.

## What Changes

- **Аддитивная коррекция на датчик**: пользователь задаёт сдвиг (°C) для трёх температур: `t1` (DS18B20), `t2` (DS18B20), `outside` (уличная). Исправленное значение = сырое + сдвиг.
- **Единая абстракция чтения**: вводится порт `ITemperatureSource`. Для `t1`/`t2`/`outside` он возвращает скорректированные значения; температуры котла CH supply/CH return/DHW отдаются без коррекции. Все потребители температуры переходят на порт и больше не читают сырые значения из `IHeatingStateStore`.
- **Настройка и хранение**: сдвиги задаются через `POST /api/control`, отображаются в панели «Коррекция температур» во вкладке «Управление» (перед кнопкой «Выключить котёл») и сохраняются в NVS (переживают перезагрузку). MQTT-команда не добавляется.

## Capabilities

### New Capabilities

- `temperature-correction`: пользовательские аддитивные сдвиги для T1/T2/уличной температуры, применяемые через единую абстракцию чтения `ITemperatureSource`.

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/ports/driven/itemperatures_source.h` — новый порт `ITemperatureSource`.
- `main/infrastructure/driven/temperature_source_adapter.{h,cpp}` — адаптер поверх `IHeatingStateStore`.
- `main/application/ports/driven/iheating_state_store.h` — 3 пары `set_*_offset`/`get_*_offset` (`t1`, `t2`, `outside`).
- `main/infrastructure/driven/heating_state_adapter.{h,cpp}` — хранение сдвигов.
- `main/infrastructure/driven/nvs_config_store.h` — новый blob `NvsTempOffsetBlob` (3 × float).
- `main/infrastructure/driven/boiler_nvs_store.cpp` — сохранение/загрузка сдвигов.
- `main/application/ports/driving/iconfigure_system.h` + `main/application/use_cases/system_config_interactor.{h,cpp}` — сеттер сдвигов.
- `main/infrastructure/driving/http_controller_adapter.cpp` — разбор полей сдвигов в `POST /api/control`.
- `main/infrastructure/driving/web_page.h` — панель коррекции температур (вкладка «Управление», перед кнопкой «Выключить котёл»).
- Потребители температуры: `pid_poll_interactor.cpp`, `boiler_poll_interactor.cpp`, `gas_flow_estimator.{h,cpp}`, `dhw_predict_service.{h,cpp}`, `pid_quality_assessor.{h,cpp}`, `fopdt_estimator.{h,cpp}`, `web_presenter_adapter.{h,cpp}`.
- `main/main.cpp` — создание `TemperatureSourceAdapter` и внедрение в потребителей.
- `docs/api.md` / `docs/api.en.md` — документирование полей сдвигов.
