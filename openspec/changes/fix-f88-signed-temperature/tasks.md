## 1. Интерфейс и адаптер

- [ ] 1.1 Добавить `virtual ReadResult read_temperature(uint8_t data_id) = 0;` в `IBoilerHardware` (`main/application/ports/driven/iboiler_hardware.h`)
- [ ] 1.2 Реализовать `BoilerOpenThermAdapter::read_temperature()` через `OT_f88_to_float(resp.data_value)` (`main/infrastructure/driven/boiler_opentherm_adapter.{h,cpp}`)
- [ ] 1.3 Реализовать `read_temperature()` в `FakeBoilerHardware` (`test/fakes/fake_boiler_hardware.{h,cpp}`)

## 2. Переключение температурных call-site

- [ ] 2.1 Заменить `boiler_.read(OT_ID_CH_TEMP)` → `read_temperature` (`boiler_poll_interactor.cpp`)
- [ ] 2.2 Заменить `boiler_.read(OT_ID_DHW_TEMP)` → `read_temperature`
- [ ] 2.3 Заменить `boiler_.read(OT_ID_OUTSIDE_TEMP)` → `read_temperature`
- [ ] 2.4 Заменить `boiler_.read(OT_ID_RETURN_TEMP)` → `read_temperature`

## 3. Уборка мёртвого кода

- [ ] 3.1 Удалить `BoilerPollInteractor::f88_to_float()` и объявление в `.h` (`boiler_poll_interactor.{cpp,h}`)

## 4. Тесты

- [ ] 4.1 Добавить unit-тест декодирования f8.8: `0xFE00`→`-2.0`, `0x0000`→`0.0`, `0xFF80`→`-0.5`, `0xCE00`→`-50.0`, `0x1400`→`+20.0`
- [ ] 4.2 Добавить тест, что модуляция/байт-парные ID не идут через знаковый декодер
- [ ] 4.3 Собрать и прогнать существующий набор unit-тестов

## 5. Верификация

- [ ] 5.1 `openspec validate --change fix-f88-signed-temperature` (или эквивалент) без ошибок
- [ ] 5.2 Полная сборка проекта (idf.py build / cmake) без ошибок
