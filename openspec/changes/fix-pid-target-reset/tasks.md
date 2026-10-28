## 1. Code fix

- [ ] 1.1 В `PidPollInteractor::disable()` передавать в `set_pid_state()` текущие `get_pid_room_temp()` и `get_pid_target_room()` вместо 0.
- [ ] 1.2 В выключенной ветке `PidPollInteractor::execute()` передавать те же текущие значения вместо 0.

## 2. Tests

- [ ] 2.1 Добавить регрессионный тест: `disable()` → `enable()` сохраняет цель комнаты.
- [ ] 2.2 Добавить регрессионный тест: выключенный `execute()` сохраняет цель и комнатную температуру.
- [ ] 2.3 Проверить существующие lifecycle-тесты на предположение, что цель обнуляется.

## 3. Verification

- [ ] 3.1 Прогнать host-тесты: `test/run_tests.sh`.
- [ ] 3.2 Валидировать change: `openspec validate fix-pid-target-reset`.
