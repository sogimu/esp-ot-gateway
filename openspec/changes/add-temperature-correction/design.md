# Design: add-temperature-correction

## Context

Шлюз (OpenTherm-мастер для Baxi Duo-tec Compact) читает две DS18B20-температуры (`T1`, `T2`) через `ITemperatureSensor` и четыре температуры котла (`outside`, `ch_temp`, `return_temp`, `dhw_temp`) через `BoilerPollInteractor` → `BoilerOpenThermAdapter::read_temperature()`. Все шесть значений пишутся в `IHeatingStateStore`, откуда их напрямую читают потребители: PID (`pid_poll_interactor`), газовая модель (`gas_flow_estimator`), DHW-гистерезис/прогноз (`boiler_poll_interactor`, `dhw_predict_service`), FOPDT (`fopdt_estimator`), оценка качества PID (`pid_quality_assessor`) и веб-интерфейс (`web_presenter_adapter`).

Сегодня нет единой точки, где к показанию применялась бы пользовательская поправка, поэтому смещение датчика искажает все расчёты сразу.

## Goals / Non-Goals

**Goals:**
- Пользовательский аддитивный сдвиг (°C) для каждой из шести температур, переживающий перезагрузку.
- Единственная абстракция чтения температуры, сквозь которую идут все потребители, — сырые значения из `IHeatingStateStore` в прикладной логике больше не читаются.

**Non-Goals:**
- Не добавляем масштабный коэффициент (линейную коррекцию `raw * k + offset`) — только сдвиг.
- Не добавляем MQTT-команду для коррекции (только HTTP + веб + NVS).
- Не трогаем декодирование f8.8 (`opentherm-temperature-decoding`) — коррекция применяется после декодирования.
- Не меняем газовую модель и прочие подсистемы, кроме замены источника чтения температуры.

## Decisions

### D1. Коррекция на стороне чтения — порт `ITemperatureSource`

Вводим порт `ITemperatureSource` (driven) с шестью геттерами, возвращающими **уже скорректированные** температуры:

```cpp
class ITemperatureSource {
public:
    virtual float t1()          const = 0;  // DS18B20 T1 (corr)
    virtual float t2()          const = 0;  // DS18B20 T2 (corr)
    virtual float outside()     const = 0;  // boiler outside (corr)
    virtual float ch_flow()     const = 0;  // boiler CH supply (corr)
    virtual float ch_return()   const = 0;  // boiler CH return (corr)
    virtual float dhw()         const = 0;  // boiler DHW (corr)
    virtual ~ITemperatureSource() = default;
};
```

Адаптер `TemperatureSourceAdapter` (infrastructure/driven) держит ссылку на `IHeatingStateStore`, берёт под `lock_shared()` сырое значение и сдвиг и возвращает `raw + offset`.

- Писатели (`SensorsPollInteractor`, `BoilerPollInteractor`) продолжают писать **сырые** значения в store — сырое значение остаётся доступным для диагностики.
- Все потребители переходят на `ITemperatureSource`; прямые вызовы `state_.get_*_temp()` в прикладной логике удаляются.

*Альтернатива:* применять сдвиг на стороне записи (писать уже скорректированное) — отвергнуто, теряется сырое значение и буквально не выполняется требование «читать только через абстракцию».

### D2. Сдвиги живут в `IHeatingStateStore` и NVS

Добавляем в `IHeatingStateStore` шесть пар `set_*_offset(float)` / `get_*_offset()` (`t1`, `t2`, `outside`, `ch_temp`, `return_temp`, `dhw_temp`). Это согласуется с существующим паттерном (конфиг котла `gas_temp_offset`, `ch_pmin` и т.п. уже хранится в store).

Персистенция — новый packed-blob `NvsTempOffsetBlob` (6 × float) в namespace `config` под ключом `temp_off`, читается/пишется в `BoilerNvsStore::load_boiler_config` / `save_boiler_config` рядом с `boiler`/`eff` blobs. Отдельный blob выбран, чтобы не трогать миграцию `NvsCalibBlob` (проверку `sz == 40` / `sz == sizeof(NvsCalibBlob)`).

*Альтернатива:* отдельный `ITemperatureCorrectionStore` + NVS-store — отвергнуто ради минимума новых классов; store уже является точкой синхронизации конфигурации.

### D3. Валидность показаний

Коррекция аддитивная и безусловная (`raw + offset`). Источником истины о валидности остаются существующие механизмы: сентинел `-127.0f` для DS18B20 и флаги `*_valid_` в `gas_flow_estimator`. Сдвиг ограничен диапазоном `[-10.0, +10.0]` °C, поэтому сентинел `-127.0f` не может быть «поднят» выше порога валидности (`> -100.0f`) ошибочным сдвигом.

### D4. Валидация и границы сдвигов

`set_temperature_offsets(...)` в `SystemConfigInteractor` обрезает каждый сдвиг до `[-10.0, +10.0]` °C (константы в одном месте). HTTP-обработчик парсит поля и передаёт их единым сеттером; партиального обновления не требуется (как у CH/DHW power range: поля, отсутствующие в запросе, не меняют хранимое значение).

### D5. Имена полей API и веб-панель (единообразная)

Поля `POST /api/control`: `t1_offset`, `t2_offset`, `outside_offset`, `ch_temp_offset`, `return_temp_offset`, `dhw_temp_offset`.

Веб-панель «Коррекция температур» полностью повторяет паттерн панели «Модель котла» (`mf_*`):

- Поля ввода — те же inline `input type='number'` со стилем `width:60px;background:#1a1a2e;color:#4fc3f7;border:1px solid #0f3460;border-radius:4px;padding:2px 4px;text-align:center;font-size:.9em`, внутри `.info-row` / `.info-label` / `.info-val`, с `.tip`-подсказками. У каждого поля `step='0.1'`, диапазон `min='-10' max='10'`, `oninput='setTempCorrDirty(true)'`.
- Единый dirty-флаг панели `dirtyFields.temp` через `setTempCorrDirty(on)`, который подсвечивает все шесть полей (`background` `#2a1a2e` при dirty, `#1a1a2e` при чистом) — по образцу `setModelDirty`.
- Кнопка Apply (`.btn`) + span сообщения (`#temp_msg`); при успехе вызывает `setTempCorrDirty(false)`, при ошибке показывает сообщение.
- Защита от поллинга: в `poll()` значения полей обновляются только при `!dirtyFields.temp` (аналог `!dirtyFields.model` для `mf_*`).

## Risks / Trade-offs

- **Большой размах рефакторинга** (7 потребителей) → Mitigation: механическая замена `state_.get_X_temp()` → `temp_source_.X()`; поведение без сдвигов (`offset == 0`) идентично, что проверяется существующими тестами.
- **Сдвиг применяется и к сырым диагностическим показам** (веб-интерфейс показывает скорректированное) → Mitigation: это и есть цель; сырое значение остаётся в store для отладки, но не экспонируется в прикладной логике.
- **Двойное применение сдвига** (если где-то забудут перейти на `ITemperatureSource`) → Mitigation: задача-список потребителей в tasks.md + тест, фиксирующий, что прикладные сервисы получают источник, а не store.

## Migration Plan

1. Добавить порт `ITemperatureSource`, поля сдвигов в store и адаптер.
2. Добавить NVS-персистенцию сдвигов.
3. Добавить сеттер в `IConfigureSystem`/`SystemConfigInteractor` и разбор в HTTP.
4. Добавить веб-панель коррекции.
5. Перевести всех потребителей температуры на `ITemperatureSource`.
6. Тесты + документация + сборка.

## Open Questions

- Нет.
