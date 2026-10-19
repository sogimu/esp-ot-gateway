## Why

Уставки CH/DHW обрезались по захардкоженным диапазонам, разбросанным по HTTP-обработчику и use case (`[20,80]` CH, `[35,65]`/`[35,80]` DHW), которые не совпадали с реальными границами котла (ID 49/48) и друг с другом. Например, DHW-уставку можно было задать до 80 °C, хотя котёл поддерживает максимум 60 °C.

## What Changes

- **Единый источник истины для границ уставок**: диапазоны CH/DHW берутся из ID 49/48, которые сообщает котёл, и обрезают значения во всех точках входа (HTTP, MQTT, расписание, use case), не шире, чем поддерживает котёл.
- **Переобжим на пути записи CH**: уставка CH, записываемая в котёл, дополнительно обрезается по верхней границе котла (как уже делает DHW), чтобы значение из NVS или другого пути не превысило максимум котла.

## Capabilities

### New Capabilities

- `setpoint-bounds`: обрезка уставок CH/DHW по границам котла (ID 49/48) как единый источник истины.

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/system_config_interactor.cpp` — обрезка уставок по границам котла.
- `main/infrastructure/driving/http_controller_adapter.cpp` — убраны хардкод-диапазоны в `POST /api/control` и `POST /api/schedule`.
- `main/infrastructure/driving/mqtt_interactor.cpp` — убраны хардкод-диапазоны уставок.
- `main/application/use_cases/boiler_poll_interactor.cpp` — переобжим CH-уставки по верхней границе котла (ID 49).
- `docs/api.md` / `docs/api.en.md` — актуализация диапазонов.
