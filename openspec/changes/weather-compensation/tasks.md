## 1. Модель состояния и персист

- [x] 1.1 Добавить `wc_sp_cold` / `wc_sp_warm` в `IHeatingStateStore` и `HeatingStateAdapter` (+ поля состояния, дефолты 30/25).
- [x] 1.2 Добавить в `FakeHeatingStateStore`.
- [x] 1.3 Загрузка/сохранение в `BoilerNvsStore` (ключи `wc_spc`, `wc_spw`).

## 2. Настройка через API

- [x] 2.1 `IConfigurePid::set_wc_curve(cold, warm)` + реализация в `SystemConfigInteractor`.
- [x] 2.2 Разбор `wc_sp_cold` / `wc_sp_warm` в `POST /api/control`.

## 3. Погодная база

- [x] 3.1 В `PidPollInteractor` вычислять `base` по кривой из `temps_->outside()` (fallback 26).
- [x] 3.2 Удалить обучение базы (`BASE_TAU`, слив, перенос, `base_inited_`, `last_auto_output_`).
- [x] 3.3 `SP = clamp(base + trim, sp_lo, sp_hi)`; trim-bounds c учётом base.

## 4. Веб

- [x] 4.1 Два ползунка «Подача при −20 °C» / «Подача при +15 °C».
- [x] 4.2 Отправка в `/api/control`, отображение в статусе.

## 5. Проверка

- [x] 5.1 Тесты: интерполяция, кламп, fallback.
- [x] 5.2 Обновить/убрать тесты обучения базы (обучения больше нет).
- [x] 5.3 Собрать host-тесты и прошивку.
