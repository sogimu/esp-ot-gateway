# Tasks: add-temperature-correction

## 1. Порт `ITemperatureSource` и хранение сдвигов

- [x] 1.1 Добавить `main/application/ports/driven/itemperatures_source.h` с `ITemperatureSource` (6 геттеров)
- [x] 1.2 Добавить в `IHeatingStateStore` 6 пар `set_*_offset`/`get_*_offset` (`t1`, `t2`, `outside`, `ch_temp`, `return_temp`, `dhw_temp`)
- [x] 1.3 Реализовать поля сдвигов в `HeatingStateAdapter` (дефолт `0.0f`)
- [x] 1.4 Добавить `TemperatureSourceAdapter` (`infrastructure/driven/temperature_source_adapter.{h,cpp}`): `raw + offset` под `lock_shared()`

## 2. Персистенция в NVS

- [x] 2.1 Добавить `NvsTempOffsetBlob` (6 × float) в `nvs_config_store.h`
- [x] 2.2 В `boiler_nvs_store.cpp` `load_boiler_config`: читать blob `temp_off` и раскладывать в store
- [x] 2.3 В `boiler_nvs_store.cpp` `save_boiler_config`: собирать blob из store и писать `temp_off`

## 3. Сеттер и HTTP API

- [x] 3.1 Добавить `set_temperature_offsets(float t1, float t2, float outside, float ch_temp, float return_temp, float dhw_temp)` в `IConfigureSystem`
- [x] 3.2 Реализовать в `SystemConfigInteractor` с обрезкой каждого сдвига до `[-10.0, +10.0]`
- [x] 3.3 Разобрать поля `t1_offset`, `t2_offset`, `outside_offset`, `ch_temp_offset`, `return_temp_offset`, `dhw_temp_offset` в `http_controller_adapter.cpp` `handler_control`

## 4. Веб-интерфейс

- [x] 4.1 Добавить панель «Коррекция температур» в `web_page.h` (6 полей + кнопка Apply + span `#temp_msg`) в `.info-row`/`.info-label`/`.info-val`
- [x] 4.2 Стилизовать поля ввода единообразно с `mf_*` (inline `input type='number'`, `width:60px`, `background:#1a1a2e`, `color:#4fc3f7`, `border:1px solid #0f3460`, `border-radius:4px`, `step='0.1'`, `min='-10'`, `max='10'`, `.tip`-подсказки)
- [x] 4.3 Реализовать `setTempCorrDirty(on)` с флагом `dirtyFields.temp` и подсветкой фона (`#2a1a2e`/`#1a1a2e`), `oninput='setTempCorrDirty(true)'` на каждом поле
- [x] 4.4 Добавить защиту от поллинга: в `poll()` обновлять поля только при `!dirtyFields.temp`
- [x] 4.5 Реализовать `applyTempCorr()`: отправка сдвигов в `POST /api/control`, при успехе `setTempCorrDirty(false)` + сообщение, при ошибке сообщение об ошибке

## 5. Перевод потребителей на `ITemperatureSource`

- [x] 5.1 `pid_poll_interactor` — комнатная температура через `temp_source_.t1()/t2()`
- [x] 5.2 `boiler_poll_interactor` — `dhw_temp` в `do_dhw_hysteresis` через `temp_source_.dhw()`
- [x] 5.3 `gas_flow_estimator` — `t_ret`/`t_flow`/`t_out` через `ch_return()/ch_flow()/outside()`
- [x] 5.4 `dhw_predict_service` — `dhw_temp` через `temp_source_.dhw()`
- [x] 5.5 `pid_quality_assessor` — `outside_temp` через `temp_source_.outside()`
- [x] 5.6 `fopdt_estimator` — `outside` через `temp_source_.outside()`
- [x] 5.7 `web_presenter_adapter` — все шесть температур через `temp_source_`
- [x] 5.8 `main.cpp` — создать `TemperatureSourceAdapter` и внедрить `ITemperatureSource&` в потребителей

## 6. Тесты

- [x] 6.1 Добавить `test/test_temperature_correction.cpp` (аддитивность, границы ±10, независимость сдвигов)
- [x] 6.2 Обновить `test/fakes/fake_heating_state_store.h` (поля сдвигов)
- [x] 6.3 Проверить, что существующие тесты потребителей проходят с `offset == 0`

## 7. Документация и проверка

- [x] 7.1 Обновить `docs/api.md` и `docs/api.en.md` (поля сдвигов, диапазон ±10)
- [x] 7.2 Запустить `./test/run_tests.sh`
- [x] 7.3 Собрать прошивку по `docs/build.md`
