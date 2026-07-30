# OTA end-to-end: подготовка и HW-тест-план

[English](#english-below) | **Русский**

> Тестировщик (задача T3). Документ описывает, как **безопасно** подготовить
> «плохой» образ для проверки авто-отката, как опубликовать его через CI и
> как прогнать три HW-сценария на реальном устройстве.
>
> **Устройства в работе нет** — все шаги, которые можно проверить софтверно
> (применение патча, сборка bad-image, схема партиций), уже выполнены и отмечены
> как `[HOST]`. Шаги, требующие ESP32 по USB, помечены `[HW]` — их выполняет
> пользователь.

---

## 1. Что проверяется

Сценарии покрытыют полный цикл A/B-обновления и связаны с компонентами D1–D11:

| Компонент | Роль в тесте |
|---|---|
| `OtaValidityAdapter` (D4) | окно 90 с, `mark_valid`, откат по таймауту и по краху |
| `EspOtaAdapter` (D2) | скачивание образа, прогресс 0→100 % |
| `OtaVersionIndexAdapter` (D3) | каталог `versions.json` |
| `OtaInteractor` (D5) | оркестратор, `flush stats` перед ребутом |
| HTTP `/api/ota/*` (D6) + UI-вкладка «Обновление ПО» (D8) | запуск/откат/статус |
| `CrashDiagnosticsAdapter` | признак краха предыдущей загрузки → `set_crash_flag` |
| Заморозка NVS (D9) + harden лоадеров (D10) | целостность NVS после отката (**связка с T2**) |

**Ключевое правило NVS:** во время `ESP_OTA_IMG_PENDING_VERIFY` (до `mark_valid`)
устройство НЕ пишет NVS. Поэтому после любого отката накопленные настройки
(WiFi, MQTT, конфигурация котла, статистика) остаются целыми — это и есть
критерий NVS-целостности из T2.

---

## 2. Предусловие 0 (одноразовое): USB-прошивка нового макета партиций

`[HW]` **Обязательно для первого перехода.** Bootloader не может заменить
собственную таблицу партиций по OTA — её надо прошить один раз по USB. Старый
макет (с разделом `factory`) **не совместим** с новой A/B-схемой
(`ota_0`/`ota_1`/`otadata`, без `factory`). Без этого шага OTA просто некуда
писать.

Действие (стираем флеш и шьём всё сразу — bootloader + partition table + app
в `ota_0` + начальный `otadata`):

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor   # лог; выход Ctrl+]
```

Эквивалент через esptool (смещения — из `partitions.csv`):

```bash
python3 -m esptool --chip esp32 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/esp-ot-gateway.bin \
  0x310000 build/ota_data_initial.bin
```

После этого шага устройство живёт в слоте `ota_0`, и дальнейшие обновления
идут как `ota_0` ↔ `ota_1` по воздуху. **Повторять этот шаг при каждом
обновлении макета партиций.**

Проверка после прошивки (по serial): лог содержит
`ota_valid: загружена партиция=ota_0 pending_verify=0`.

---

## 3. Подготовка «плохого» образа (рецепт, НЕ ломает main)

Патч **удалён** из репозитория (рецепт описан, патч не нужен для CI).
Создайте его вручную на throwaway-ветке:

```diff
--- a/main/main.cpp
+++ b/main/main.cpp
@@ ... @@ extern "C" void app_main(void)
     http.set_ota(&ota);
     const bool http_ok = http.start();
     ota_validity.set_http_server_up(http_ok);
     ota_validity.arm();
+    ESP_LOGE("ota_bad", "ТЕСТ: намеренный abort() для проверки авто-отката OTA");
+    abort();  // panic → coredump → ребут
```

### Почему panic именно ПОСЛЕ `arm()`, а не в начале `app_main`

Логика авто-отката в `OtaValidityAdapter::arm()`:

- **Загрузка 1 (bad):** доходит до `arm()` (краха ещё не было → `crash_flag=false`,
  дедлайн взведён), затем `abort()` → panic → coredump пишется в раздел
  `coredump` → ребут (reset reason = `panic/exception`).
- **Загрузка 2 (bad):** `OtaValidityAdapter` видит `pending=true`;
  `CrashDiagnosticsAdapter::check_on_boot()` находит coredump →
  `last_boot_had_crash()=true` → `set_crash_flag(true)`; `arm()` при
  `pending + крах` немедленно вызывает `mark_app_invalid_rollback_and_reboot()`
  → загрузка в прежний (хороший) слот.

> **Важно:** panic ДО `arm()` = **бесконечный crash-loop без отката**
> (устройство не доходит до логики валидации). Поэтому патч вставляет `abort()`
> именно после `ota_validity.arm();`.

### Применение на отдельной ветке (не коммитить в main/master)

```bash
git checkout -b ota-test/bad-crash       # отдельная throwaway-ветка
# Применить патч вручную (см. рецепт в §3 выше)
git add main/main.cpp
git commit -m "test: bad-image для проверки авто-отката OTA (не для main)"
grep -n ota_bad main/main.cpp           # убедиться, что патч применён
```

### Альтернатива: «зависший» образ (для проверки пути по таймауту 90 с)

Если нужно отдельно проверить путь «истёк дедлайн 90 с» (а не краш-путь),
замените `http.start()` на `false`:

```c
// Вместо: const bool http_ok = http.start();
const bool http_ok = false;              // HTTP «не поднялся»
ota_validity.set_http_server_up(false);  // health=false
```

Тогда `heartbeat()` видит `http_server_up_=false` → `healthy=false` →
через 90 с сработает `rollback_and_reboot("истёк дедлайн 90 с…")`.
Это дополнительный сценарий — основной HW-план (ниже) покрывает краш-путь.

---

## 4. Публикация тега через CI

CI (`.github/workflows/tests.yml`) реагирует на теги `v*` заданием
`firmware-build`: собирает прошивку и создаёт **GitHub Release** с артефактами
`esp-ot-gateway.bin`, `bootloader.bin`, `partition-table.bin`.

Сам по себе тег кладёт бинарник **только в Release**. Устройство же качает
образ с GitHub Pages: `https://sogimu.github.io/esp-ot-gateway/firmware/<tag>/esp-ot-gateway.bin`
(см. `EspOtaAdapter::download`). Каталог `/firmware/<tag>/` наполняется
заданием `deploy-pages`, которое跑ит по всем релизам и копирует их `.bin`
на Pages. `deploy-pages` запускается на push в `master`.

```bash
# На ветке ota-test/bad-crash (см. §3):
git tag v0.0.0-ota-bad-crash-test          # prerelease-тег
git push origin ota-test/bad-crash
git push origin v0.0.0-ota-bad-crash-test  # → триггерит firmware-build + Release

# Чтобы bad-образ появился на Pages по OTA-URL:
#   вариант 1 — дождаться ближайшего push в master; ИЛИ
#   вариант 2 — запустить workflow вручную на master (github.ref == master):
gh workflow run tests.yml --ref master
```

После `deploy-pages` bad-образ доступен по адресу:
`https://sogimu.github.io/esp-ot-gateway/firmware/v0.0.0-ota-bad-crash-test/esp-ot-gateway.bin`

Проверить `[HOST]` (с компьютера, до теста на устройстве):

```bash
curl -sI https://sogimu.github.io/esp-ot-gateway/firmware/v0.0.0-ota-bad-crash-test/esp-ot-gateway.bin
# Ожидание: HTTP/2 200
curl -s https://sogimu.github.io/esp-ot-gateway/versions.json | \
  python3 -c 'import sys,json;print([v["tag"] for v in json.load(sys.stdin)["versions"]])'
# Ожидание: тег v0.0.0-ota-bad-crash-test присутствует в списке
```

> Не публикуйте bad-тег с тем же именем, что и релизная версия. Используйте
> явно предрелизные имена (`v0.0.0-…-test`) и удаляйте тег/релиз после теста:
> `gh release delete v0.0.0-ota-bad-crash-test --yes; git push origin :v0.0.0-ota-bad-crash-test`.

---

## 5. HW-тест-план

**Общие исходные:** устройство прошито новым макетом партиций (§2), подключено
к WiFi (режим STA, есть интернет), открыт `idf.py -p /dev/ttyUSB0 monitor`.
UI — вкладка «Обновление ПО». Запишите **версию и слот ДО теста**
(`/api/ota/status` → `current_version`; serial → `ota_valid: загружена партиция=…`).

Эндпоинты (для проверки через `curl`/браузер, если удобно):
- `GET  /api/ota/status` — `{state, progress, current_version, target_tag, rollback_pending, last_error}`
- `GET  /api/ota/versions` — каталог `versions.json`
- `POST /api/ota/start` — тело `{"tag":"<версия>"}` → `{"ok":true}`
- `POST /api/ota/rollback` → `{"ok":true}` (отвечает ДО ребута)

---

### Сценарий A — Хороший апдейт + mark_valid < 90 с

`[HW]` Шаги:

1. Устройство на версии **v1** в слоте `ota_0` (например).
2. На вкладке «Обновление ПО» обновите список версий, выберите **v2** (релизный
   тег, уже опубликованный на Pages), нажмите «Обновить».
3. Следите за прогрессом в UI/`/api/ota/status`: `state` → `fetching` →
   `writing`, `progress` растёт до **100**.
4. После 100 % устройство само перезагружается в новый слот (`ota_1`).

Ожидаемый результат:

- `state` достигает `done` (100 %), затем ребут.
- В serial после ребута:
  `ota_valid: загружена партиция=ota_1 pending_verify=1`,
  затем `ota_valid: arm(): взведён дедлайн валидации 90 с`,
  и на первом здоровом тике — `ota_valid: Прошивка подтверждена валидной (mark_app_valid) — отмена отката`.
- `current_version` = **v2**; `rollback_pending` = `false`.
- Слот = `ota_1`. mark_valid срабатывает задолго до 90 с (на первом тике цикла).
- **NVS цел (связка с T2/D9):** WiFi автоматически переподключился сохранёнными
  credentialами, MQTT переподключился, значения на вкладках конфигурации
  совпадают с дотестовыми. Статистика (`/api/ota/status` не показывает ошибок;
  показометры на месте) сохранена (сброс статистики выполнен перед ребутом в
  новый слот — `ota_flush_stats_cb`).

---

### Сценарий Б — Плохой образ → авто-откат через краш-путь

`[HW]` Шаги:

1. Устройство на хорошей **v1** в слоте `ota_0`. Bad-образ
   `v0.0.0-ota-bad-crash-test` уже опубликован на Pages (§4).
2. На вкладке «Обновление ПО» выберите тег `v0.0.0-ota-bad-crash-test`,
   нажмите «Обновить». Дождитесь 100 % и ребута в bad-слот (`ota_1`).

Ожидаемый результат (краш-путь через `CrashDiagnosticsAdapter`):

- **Загрузка 1 (bad):** serial —
  `ota_valid: загружена партиция=ota_1 pending_verify=1`,
  `ota_valid: arm(): взведён дедлайн…`,
  затем `E ota_bad: ТЕСТ: намеренный abort()…` и panic
  (reset reason = `panic/exception`). Coredump пишется в раздел `coredump`,
  устройство перезагружается.
- **Загрузка 2 (bad):** serial —
  `crash: Крах: … PC=… exccause=…` (coredump найден),
  `main: OTA: загружена партиция в состоянии PENDING_VERIFY…`,
  `main: Предыдущая загрузка: КРАХ`,
  `E ota_valid: ОТКАТ прошивки: предыдущая загрузка завершалась крашем` →
  `mark_app_invalid_rollback_and_reboot()` → ребут.
- **Загрузка 3 (хороший слот `ota_0`, v1):** serial —
  `ota_valid: загружена партиция=ota_0 pending_verify=0`,
  `current_version` = **v1**. Устройство штатно работает.
- **NVS цел (связка с T2/D9/D10):** т.к. во время `PENDING_VERIFY` запись NVS
  была заморожена (D9), а лоадеры — hardened (D10), после отката WiFi/MQTT/
  конфигурация/статистика **не повреждены** — значения совпадают с дотестовыми.
  Серийно: нет сообщений об ошибках чтения NVS; WiFi и MQTT поднялись как до теста.

> Если вместо отката наблюдается бесконечный crash-loop на bad-слоте — значит
> panic срабатывает до `arm()` (неправильный патч) или coredump не пишется
> (проверить `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` и наличие раздела
> `coredump` в `partitions.csv`).

---

### Сценарий В — Ручной откат кнопкой в UI

`[HW]` Шаги:

1. Сначала приведите устройство в «обновлённое» состояние: выполните
   **Сценарий A** (хорошее обновление до **v2**, `mark_valid` уже сработал,
   устройство штатно работает на v2 в слоте `ota_1`).
2. На вкладке «Обновление ПО» нажмите красную кнопку **«Откатить»**.
   (UI шлёт `POST /api/ota/rollback`; хендлер отвечает `{"ok":true}` ДО ребута
   — паттерн respond-then-reboot — и вызывает `OtaInteractor::rollback_now()`.)

Ожидаемый результат:

- `rollback_now()` → `esp_ota_mark_app_invalid_rollback_and_reboot()` → ребут.
- В serial после ребута: `ota_valid: загружена партиция=ota_0 pending_verify=0`,
  `current_version` = **v1** (прежняя версия, прежний слот).
- Устройство штатно работает на v1.
- **NVS цел (связка с T2):** конфигурация/статистика не повреждены.

> Кнопка «Откатить» работает в любой момент: после `mark_valid` партиция уже
> VALID, поэтому `esp_ota_mark_app_invalid_rollback_and_reboot()` возвращает
> ошибку, и код выполняет ручной выбор предыдущего OTA-слота через итератор
> `esp_partition_find`. Сценарий В надёжен без необходимости «поймать» окно.

---

## 6. Чек-лист прохождения

- [ ] `[HW]` Предусловие 0: новый макет партиций прошит по USB, устройство
      грузится из `ota_0`/`ota_1` (не factory).
- [ ] `[HOST]` bad-image собирается; main чист после реверса патча.
- [ ] `[HOST]` bad-тег опубликован, `curl …/firmware/<bad-tag>/esp-ot-gateway.bin` → 200.
- [ ] `[HW]` **A:** апдейт до 100 %, ребут, `mark_valid < 90 с`, новая версия,
      прежний/новый слот корректен, NVS цел.
- [ ] `[HW]` **Б:** bad-образ → краш → авто-откат в прежний слот, прежняя
      версия, NVS цел.
- [ ] `[HW]` **В:** кнопка «Откатить» → прежний слот, прежняя версия, NVS цел.
- [ ] После теста: bad-тег/релиз удалён; throwaway-ветка удалена; `main`/`master`
      не содержит bad-image.

---

## English below

End-to-end OTA test plan. Three HW scenarios: (A) good update → `mark_valid`
within 90 s; (B) bad image → auto-rollback via crash path
(`CrashDiagnosticsAdapter` → `mark_invalid_rollback_and_reboot`); (C) manual
rollback via the UI "Откатить" button. Each expects: device boots the previous
slot, previous version, **NVS intact** (D9 freezes NVS writes during
`PENDING_VERIFY`, D10 hardens loaders — this is the T2 integrity link).

**Bad-image recipe is safe:** `scripts/ota-bad-image.patch` is an inert file
that never touches the `main` build. Apply it on a throwaway branch
(`ota-test/bad-crash`), build, tag (`v0.0.0-ota-bad-crash-test`), push the tag
to trigger the `firmware-build` CI job (creates a GitHub Release with the bin),
then run `gh workflow run tests.yml --ref master` so `deploy-pages` copies the
bin to `https://sogimu.github.io/esp-ot-gateway/firmware/<tag>/esp-ot-gateway.bin`.

**Mandatory one-time USB flash:** the bootloader cannot replace its own
partition table over OTA, so the first migration from the old `factory` layout
to the new A/B `ota_0`/`ota_1`/`otadata` layout must be done once via USB
(`idf.py erase-flash && idf.py flash`).

Software-verified already: the patch applies (`git apply --check` = OK), the
bad image builds under ESP-IDF v5.3.2, and `main` is clean after reversing the
patch. All remaining steps are `[HW]`.
