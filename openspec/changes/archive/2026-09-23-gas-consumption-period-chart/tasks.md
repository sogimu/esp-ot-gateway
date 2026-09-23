## 1. Доменный слой: сбор данных

- [x] 1.1 В `igas_correction_store.h` заменить `GasDailyBlob`/`GAS_DAILY_SLOTS` на `GasTodayBlob` (`today_epoch_day`, `today_m3`, `today_hours[24]`, `current_hour`, `current_hour_m3`) и `GasHistoryBlob` (`daily` 64 слота `{epoch_day,m3}`, `yesterday_hours[24]`, `yesterday_epoch_day`); заменить методы `save/load_daily_gas` на пары `save/load_today_gas` + `save/load_history_gas`.
- [x] 1.2 В `GasFlowService` добавить почасовое накопление: константы `HOURLY_SLOTS=48`, `DAILY_SLOTS=64`, кольцо `hourly_[48]`, аккумулятор `hourly_accumulator_`, флаг текущего часа `today_epoch_hour_`.
- [x] 1.3 Реализовать `update_hourly_tracking()` (граница часа архивирует в `hourly_`, зануляет пропущенные часы) и вызывать его из `execute()` при синхронизированном времени.
- [x] 1.4 Обновить `update_daily_tracking()` под 64 слота; при смене суток переносить `hourly_` (сегодня → вчера) и архивировать день.
- [x] 1.5 Обновить `pack/load` и `get_daily_view`/новый `get_hourly_view` под новые структуры; `reset()` чистит оба кольца и часовой аккумулятор.

## 2. Персистентность

- [x] 2.1 В `GasCorrectionNvsStore` добавить `save/load_today_gas` (ключ `today`, namespace `meter`) и `save/load_history_gas` (ключ `history`), с проверкой `sz == sizeof(...)`.
- [x] 2.2 В `PersistenceLoopInteractor::tick()`: частое сохранение `GasTodayBlob`; редкое (на границе суток) сохранение `GasHistoryBlob`.
- [x] 2.3 В `GasCorrectionInteractor` и `fake_gas_correction_store.h` привести сигнатуры под новые методы.

## 3. Презентация и API

- [x] 3.1 В `WebPresenterAdapter` добавить `render_gas_history(char* buf, size_t)`: daily с `epoch_day`/`ym`/`d`/`m3`/`today`, hourly с `epoch_hour`/`h`/`m3` (подписи через `civil_from_seconds`).
- [x] 3.2 В `HttpControllerAdapter` добавить маршрут `GET /api/gas-history` со своим буфером (≥ 8 КБ) и хэндлером.
- [x] 3.3 Убрать секцию `"daily"` из `render_stats()` (оставить `render_stats`/MQTT без изменений).

## 4. Веб-интерфейс

- [x] 4.1 В `web_page.h` добавить ComboBox (`<select>`) с шестью пунктами над графиком и обработчик `onchange`.
- [x] 4.2 Переписать `drawGasChart()` в общий рендер баров `{label, value, today?}` с итогом за период в шапке и нативным тултипом (`<title>`); убрать зависимость от `d.daily`.
- [x] 4.3 Добавить `pollGasHistory()` (fetch `/api/gas-history`, свой таймер) и перерисовку при смене пункта без перезагрузки.
- [x] 4.4 Реализовать слайсинг по диапазонам на фронте: сегодня/вчера (по часам), последние 7/30 дней, этот/прошлый месяц (по `ym`).
- [x] 4.5 Обработать пустое состояние (нет данных) и будущие часы сегодня (нулевые бары).

## 5. Тесты

- [x] 5.1 Обновить `test/test_gas_daily_tracking.cpp` под новые константы/структуры; добавить тесты часовой границы, прыжка часов, `reset`, персистентности today/history.
- [x] 5.2 Обновить `test/test_gas_integration.cpp` под новый формат JSON истории; добавить проверку валидности `render_gas_history`.
- [x] 5.3 Прогнать сборку и тестовый таргет (`CMakeLists.txt`/`test/`), убедиться в отсутствии регрессий.
