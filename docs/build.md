# Сборка и отладка

[English](build.en.md) | **Русский**

> ⚠️ **Заметка мейнтейнеру:** пункты с пометкой `TODO(verify)` нужно сверить с реальными скриптами и целями CMake перед публикацией.

## Требования

| | |
|---|---|
| ОС | Linux (проверено на Ubuntu 22.04/24.04). macOS — с ручной установкой ESP-IDF. Windows: WSL2 + [usbipd](https://learn.microsoft.com/windows/wsl/connect-usb) для проброса COM-порта |
| ESP-IDF | **v5.3.x** (сборка на других мажорных версиях не гарантируется) |
| Диск / память | ~4 ГБ под тулчейн, 4 ГБ ОЗУ достаточно |
| Железо | плата ESP32 по USB (видна как `/dev/ttyUSB0` или `/dev/ttyACM0`) |

## Вариант А — установка одной командой (Ubuntu)

```bash
bash scripts/setup.sh          # ставит системные пакеты, ESP-IDF v5.3.x и тулчейн в ~/esp
```

## Вариант Б — ручная установка ESP-IDF

1. Системные пакеты:

   ```bash
   sudo apt-get update && sudo apt-get install -y \
     git wget flex bison gperf python3 python3-pip python3-venv \
     cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
   ```

2. Клонирование и установка ESP-IDF:

   ```bash
   mkdir -p ~/esp && cd ~/esp
   git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf && ./install.sh esp32
   ```

3. Активация окружения (**в каждом новом терминале**, либо добавьте алиас в `.bashrc`):

   ```bash
   source ~/esp/esp-idf/export.sh
   # удобный алиас:  echo "alias get_idf='. ~/esp/esp-idf/export.sh'" >> ~/.bashrc
   ```

## Доступ к COM-порту

```bash
sudo usermod -aG dialout $USER    # затем выйдите из системы и войдите заново
ls -l /dev/ttyUSB*                # плата должна быть видна; проверьте также /dev/ttyACM*
```

Если прошивка падает с *Permission denied* — дело именно в этом. Если порт не появляется вовсе — попробуйте другой кабель (кабели «только зарядка» — главный пожиратель времени) или поставьте драйвер CP210x/CH340 для вашей платы.

## Конфигурация, сборка, прошивка

```bash
cd esp-ot-gateway
idf.py set-target esp32                    # один раз на свежий checkout
idf.py menuconfig                          # опционально: см. опции проекта ниже
bash scripts/build_and_flash.sh /dev/ttyUSB0
# эквивалент: idf.py build && idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor             # лог с UART; выход — Ctrl+]
```

Полезное:

```bash
idf.py size-components        # что съедает флеш и ОЗУ
idf.py -p /dev/ttyUSB0 erase-flash   # полная очистка: настройки, WiFi, калибровка
idf.py fullclean              # снести каталог сборки, если кэш CMake сошёл с ума
```

Опции проекта в `menuconfig` (GPIO для OT TX/RX, реле, DS18B20; переключатели фич): `TODO(verify): перечислить реальные секции Kconfig.` Значения по умолчанию соответствуют схеме из README (OT TX → GPIO 4, OT RX → GPIO 16, реле → GPIO 23, DS18B20 → GPIO 15/26).

## Структура проекта

```
domain/           логика отопления, PID, прогноз ГВС, оценка газа — чистый C++, без ESP-IDF
application/      сценарии использования, связывающие domain-объекты через порты (интерфейсы)
infrastructure/   адаптеры: GPIO-драйвер OpenTherm, WiFi, HTTP-сервер, MqttSocketAdapter, NVS, SNTP
test/            хостовые юнит-тесты domain + application
scripts/          скрипты установки, сборки, прошивки, тестов и разбора крэшей
```

Смысл разделения: всё, что выше `infrastructure/`, компилируется и работает **на вашем ПК** — поэтому тестам не нужна плата.

## Запуск тестов

Набор — 400+ тестов / 1000+ проверок, выполняется на хосте, а не на чипе.

Требования к хосту: `gcc`/`g++` ≥ 11 (или clang ≥ 14), `cmake` ≥ 3.16, `ninja-build` — всё уже установлено, если вы прошли шаг 1 варианта Б.

```bash
bash scripts/build_and_flash.sh          # сборка прошивки + тесты
```

Ручной эквивалент:

```bash
cmake -S test -B build_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tests
./build_tests/run_tests
```

Выборочный запуск:

```bash
./build_tests/run_tests -r Pid            # тесты по имени
```

**Санитайзеры** (CI гоняет их на каждый push — запускайте локально перед PR):

```bash
cmake -S test -B build_asan \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build_asan && ./build_asan/run_tests
```

**Покрытие** (CI публикует отчёт на GitHub Pages):

```bash
cmake -S test -B build_cov -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build_cov && ./build_cov/run_tests
gcovr -r . build_cov --html-details -o coverage.html
```

Добавляя фичу: кладите логику в `domain/`, пишите тест первым, держите инфраструктурный адаптер тонким. В PR с непокрытой тестами domain-логикой попросят добавить тесты.

## Диагностика сбоев

Прошивка подготовлена к посмертной отладке:

- **Причина сброса** пишется в журнал событий при каждой загрузке (включение питания, panic, task/interrupt watchdog, brownout).
- **Core dump** при panic сохраняется в отдельный раздел флеша.
- Разбор крэша офлайн:

  ```bash
  bash scripts/decode_crash.sh /dev/ttyUSB0    # TODO(verify): точные аргументы
  ```

  Скрипт вытягивает дамп и символизирует бэктрейс по ELF — храните каталог `build/` **прошитой** версии, символы от другой сборки бесполезны.

Идеальный баг-репорт: расшифрованный бэктрейс + журнал событий вокруг инцидента (вкладка «Журнал») + модель котла.

## Заметки по слою OpenTherm

- Манчестерское кодирование/декодирование крутится в GPIO ISR под аппаратным таймером 500 мкс. Не добавляйте блокирующую работу в OT-задачу и не вешайте другие высокоприоритетные ISR на это ядро.
- Особенности Baxi (трюк CH2_ENABLE для клапана, отклоняемые записи уставки ГВС, пауза 100 мс между кадрами) живут в отдельном адаптере. Причуды другого производителя — в соседнем адаптере, а не в ядре протокола.
- Отказобезопасное реле на GPIO 23 обязано оставаться в логике «под напряжением = котлом управляет ESP». Никогда не инвертируйте её: мёртвый ESP32 должен вернуть управление котлу.
