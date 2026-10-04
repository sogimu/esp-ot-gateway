## 1. Доменный слой: накопление с раскладкой ГВС/Отопление

- [x] 1.1 В `igas_correction_store.h` переименовать `m3` → `m3_total` и добавить `m3_dhw` в `GasDailyEntry`; расширить `GasTodayBlob` (`today_m3_total`/`today_m3_dhw`, `today_hours_total[24]`/`today_hours_dhw[24]`, `current_hour_m3_total`/`current_hour_m3_dhw`) и `GasHistoryBlob` (`yesterday_hours_total[24]`/`yesterday_hours_dhw[24]`; `daily[64]` растёт через `GasDailyEntry`).
- [x] 1.2 В `GasFlowService` добавить DHW-аккумуляторы: `daily_accumulator_dhw_`, `hourly_accumulator_dhw_`, `today_hours_dhw_[24]`, `yesterday_hours_dhw_[24]`; в `DailyView`/`HourlyView` добавить `m3_dhw`.
- [x] 1.3 В `execute()` при интеграции (`:147-153`) добавлять `dv` к DHW-аккумуляторам, когда `dhw_active_` истинно.
- [x] 1.4 В `update_hourly_tracking()`/`update_daily_tracking()` переносить и занулять DHW-варианты часов вместе с суммарными; `push_daily` писать `m3_total` и `m3_dhw`.
- [x] 1.5 Обновить `pack_today`/`pack_history`/`load_daily`/`get_daily_view`/`get_hourly_view`/`reset` под новые DHW-поля.

## 2. Презентация и API

- [x] 2.1 В `render_gas_history()` (`web_presenter_adapter.cpp:345`) отдавать `m3_total` и `m3_dhw` для суток (`days`) и часов (`hours`) вместо `m3`.

## 3. Веб-интерфейс

- [x] 3.1 В `renderGasChart()` (`web_page.h:1419`) читать `m3_total`/`m3_dhw`, хранить в `gasChartData` массивы `values` (total) и `dhwValues`; выводить «Итого» как `ГВС: X · Отопление: Y · Всего: Z`.
- [x] 3.2 В `drawGasBars()` (`web_page.h:1461`) рисовать stacked-бар: нижний сегмент Отопление `#a5d6a7`, верхний ГВС `#ff7043`, высоты пропорциональны `m3_total − m3_dhw` и `m3_dhw` (clamp разности ≥ 0).
- [x] 3.3 Убрать красный маркер `#e94560`; «сегодня» помечать только непрозрачностью (1.0 против 0.7).
- [x] 3.4 Добавить легенду возле заголовка: `█ Отопление · █ ГВС`.
- [x] 3.5 Обновить `showGasTip()` (`web_page.h:1500`): выводить `ГВС: X м³` / `Отопление: Y м³` / `Всего: Z м³` (абсолютные, 3 знака).

## 4. Тесты и проверка

- [x] 4.1 Обновить `test/test_gas_daily_tracking.cpp`: blob-размеры, DHW-раскладку в `get_daily_view`/`get_hourly_view`, персистентность today/history, `reset`.
- [x] 4.2 Обновить `test/test_gas_integration.cpp` и `test/test_gas_flow_bugs.cpp` под `m3_total`/`m3_dhw`; проверить JSON `render_gas_history`.
- [x] 4.3 Добавить тест атрибуции по `dhw_active`: при включённом ГВС `m3_dhw` растёт, при отоплении — нет.
- [x] 4.4 Прогнать сборку и тестовый таргет, убедиться в отсутствии регрессий.
