#include "opentherm.h"

#include <string.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "opentherm";

/* ── GPIO helpers ──────────────────────────────────────────────────────────── */
/*
 * SmartTherm adapter polarity (совпадает с ihormelnyk/opentherm_library):
 *   TX: GPIO LOW  → адаптер активен (ток через шину OT) = ACTIVE
 *       GPIO HIGH → адаптер выключен (шина OT в покое)  = IDLE
 *   RX: шина OT active → GPIO HIGH
 *       шина OT idle   → GPIO LOW
 */
#define OT_TX_ACTIVE() gpio_set_level(GPIO_OT_TX, 0)
#define OT_TX_IDLE()   gpio_set_level(GPIO_OT_TX, 1)
#define OT_RX_READ()   gpio_get_level(GPIO_OT_RX)

/* ── Внутреннее состояние ──────────────────────────────────────────────────── */
#define OT_FRAME_BITS 34   /* start(1) + data(32) + stop(1) */

typedef enum {
    OT_S_IDLE = 0,
    OT_S_TX,
    OT_S_WAIT_RX,
    OT_S_RX_START,     /* принят первый фронт START-бита, ждём mid-bit */
    OT_S_RX,
    OT_S_DONE,
    OT_S_ERROR
} ot_state_t;

static volatile ot_state_t ot_state = OT_S_IDLE;

/* TX */
static volatile uint64_t ot_tx_frame;   /* 34-битный фрейм, MSB first */
static volatile int      ot_tx_idx;     /* текущий бит [0..33] */
static volatile int      ot_tx_half;    /* полубит: 0=первый, 1=второй */

/* RX */
static volatile uint64_t ot_rx_frame;
static volatile int      ot_rx_idx;
static volatile uint64_t ot_rx_last_edge;   /* время последнего mid-bit фронта, мкс */
static volatile uint32_t ot_rx_timeout_us;

/* Отладочные счётчики */
static volatile uint32_t dbg_isr_count   = 0;  /* сколько раз сработал GPIO ISR */
static volatile uint32_t dbg_tx_done     = 0;  /* сколько раз завершили TX */
static volatile uint32_t dbg_rx_done     = 0;  /* сколько раз приняли фрейм */
static volatile uint32_t dbg_timeout     = 0;  /* сколько раз таймаут */
static volatile uint64_t dbg_last_raw    = 0;  /* последний принятый raw-фрейм */

static esp_timer_handle_t ot_timer_handle;
static SemaphoreHandle_t  ot_done_sem;

/* ── Сборка 34-битного фрейма ─────────────────────────────────────────────── */
/*
 * Фрейм OpenTherm (MSB → LSB):
 *   bit 33      : START  = 1
 *   bit 32      : PARITY (чётный — сумма единиц в [31..0] чётна)
 *   bits 31..29 : MSG_TYPE (3 бита)
 *   bits 28..24 : SPARE   (5 бит, = 0)
 *   bits 23..16 : DATA_ID (8 бит)
 *   bits 15..0  : DATA_VALUE (16 бит)
 *   bit 0       : STOP   = 1  (бит 0 итогового 34-битного слова)
 */
static uint8_t frame_parity(uint32_t w)
{
    /* XOR-folding: возвращает 1 если нечётное число единиц */
    w ^= w >> 16; w ^= w >> 8; w ^= w >> 4;
    w ^= w >> 2;  w ^= w >> 1;
    return w & 1;
}

static uint64_t build_frame(const OT_Frame *f)
{
    uint32_t word = ((uint32_t)(f->msg_type & 0x07) << 28)
                  | ((uint32_t)(f->data_id)          << 16)
                  | ((uint32_t)(f->data_value));
    /* Чётный паритет (спецификация OT): бит 31 устанавливается так,
     * чтобы общее число единиц в word[31..0] было чётным. */
    if (frame_parity(word)) word |= (1UL << 31);

    /* Обернуть в start/stop */
    return ((uint64_t)1 << 33) | ((uint64_t)word << 1) | 1ULL;
}

static void parse_frame(uint64_t raw, OT_Frame *f)
{
    /* raw: бит 33=start, биты 32..1=data, бит 0=stop */
    uint32_t word = (uint32_t)((raw >> 1) & 0xFFFFFFFFULL);
    f->msg_type   = (uint8_t)((word >> 28) & 0x07);
    f->data_id    = (uint8_t)((word >> 16) & 0xFF);
    f->data_value = (uint16_t)(word & 0xFFFF);
}

/* ── Таймер 500 мкс — Manchester кодирование ──────────────────────────────── */
/*
 * Manchester (OpenTherm стандарт):
 *   Бит 1 → Active (ток, NPN вкл) в 1-м полубите, Idle (NPN выкл) во 2-м
 *   Бит 0 → Idle (NPN выкл) в 1-м полубите, Active (NPN вкл) во 2-м
 */
static void IRAM_ATTR ot_timer_cb(void *arg)
{
    switch (ot_state) {

    case OT_S_TX: {
        /* Все биты уже отправлены — предыдущий тик выдал последний полубит.
         * Теперь (через 500 мкс) возвращаем шину в idle. */
        if (ot_tx_idx >= OT_FRAME_BITS) {
            OT_TX_IDLE();
            dbg_tx_done++;
            ot_state = OT_S_WAIT_RX;
            ot_rx_timeout_us = (uint32_t)(esp_timer_get_time() / 1000)
                               + OT_RESPONSE_TIMEOUT_MS;
            break;
        }
        int bit = (ot_tx_frame >> (OT_FRAME_BITS - 1 - ot_tx_idx)) & 1;
        if (ot_tx_half == 0) {
            /* 1-й полубит: бит=1 → ACTIVE (ток), бит=0 → IDLE */
            bit ? OT_TX_ACTIVE() : OT_TX_IDLE();
            ot_tx_half = 1;
        } else {
            /* 2-й полубит: бит=1 → IDLE, бит=0 → ACTIVE */
            bit ? OT_TX_IDLE() : OT_TX_ACTIVE();
            ot_tx_half = 0;
            ot_tx_idx++;
        }
        break;
    }

    case OT_S_WAIT_RX:
    case OT_S_RX_START:
    case OT_S_RX: {
        /* Таймаут ожидания/приёма ответа */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms > ot_rx_timeout_us) {
            dbg_timeout++;
            ot_state = OT_S_ERROR;
            xSemaphoreGiveFromISR(ot_done_sem, NULL);
        }
        break;
    }

    default:
        break;
    }
}

/* ── GPIO ISR — детектирование и приём ответа котла ──────────────────────── */
/*
 * Полярность адаптера SmartTherm (совпадает с ihormelnyk/opentherm_library):
 *   RX: шина OT active (ток) → GPIO HIGH
 *       шина OT idle (покой) → GPIO LOW
 *   Idle-уровень GPIO = LOW (0).
 *
 * Двухстадийное определение START-бита (как в ihormelnyk):
 *   OT_S_WAIT_RX:   ждём POSEDGE (GPIO LOW→HIGH = шина idle→active = начало START).
 *   OT_S_RX_START:  ждём NEGEDGE (GPIO HIGH→LOW) в пределах 750 мкс = mid-bit START.
 *                    Это подтверждает START-бит и фиксирует точку отсчёта для данных.
 *   OT_S_RX:        приём 32 бит данных + STOP по фронтам с dt > 750 мкс.
 *                    Значение бита = !level (инверсия, как в ihormelnyk).
 */
static void IRAM_ATTR ot_rx_isr(void *arg)
{
    dbg_isr_count++;
    int level = OT_RX_READ();

    if (ot_state == OT_S_WAIT_RX) {
        /* Стадия 1: POSEDGE = начало START-бита (шина idle→active) */
        if (level == 1) {
            ot_state        = OT_S_RX_START;
            ot_rx_last_edge = esp_timer_get_time();
        }
    } else if (ot_state == OT_S_RX_START) {
        /* Стадия 2: NEGEDGE в пределах 750 мкс = mid-bit START (active→idle) */
        uint64_t now = esp_timer_get_time();
        uint64_t dt  = now - ot_rx_last_edge;
        if (dt < 750 && level == 0) {
            /* Подтверждён START-бит, начинаем приём данных */
            ot_state        = OT_S_RX;
            ot_rx_idx       = 0;
            ot_rx_frame     = 0;
            ot_rx_last_edge = now;   /* точка отсчёта = mid-bit START */
        } else {
            /* Ложный фронт — вернуться в ожидание */
            ot_state = OT_S_WAIT_RX;
        }
    } else if (ot_state == OT_S_RX) {
        uint64_t now = esp_timer_get_time();
        uint64_t dt  = now - ot_rx_last_edge;
        if (dt > 750) {
            /* Mid-bit переход: значение бита = инверсия уровня GPIO
             * (GPIO HIGH = шина active = после mid-bit бита 0,
             *  GPIO LOW  = шина idle   = после mid-bit бита 1) */
            ot_rx_frame     = (ot_rx_frame << 1) | (uint64_t)(!level);
            ot_rx_last_edge = now;
            ot_rx_idx++;
            /* 32 бита данных + STOP = 33 бита */
            if (ot_rx_idx >= OT_FRAME_BITS - 1) {
                dbg_rx_done++;
                dbg_last_raw = ot_rx_frame;
                ot_state = OT_S_DONE;
                xSemaphoreGiveFromISR(ot_done_sem, NULL);
            }
        }
        /* else: граничный (inter-bit) переход — dt ~500 мкс, игнорируем */
    }
}

/* ── Public API ────────────────────────────────────────────────────────────── */

float OT_f88_to_float(uint16_t v)
{
    int8_t  i = (int8_t)(v >> 8);
    uint8_t f = (uint8_t)(v & 0xFF);
    return (float)i + (float)f / 256.0f;
}

uint16_t OT_float_to_f88(float f)
{
    int16_t  i   = (int16_t)f;
    uint8_t  frc = (uint8_t)((f - (float)i) * 256.0f);
    return (uint16_t)(((uint8_t)i << 8) | frc);
}

void OT_Init(void)
{
    /* TX pin — выход */
    gpio_config_t tx_cfg = {
        .pin_bit_mask = 1ULL << GPIO_OT_TX,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&tx_cfg);
    OT_TX_IDLE();   /* idle: GPIO HIGH → адаптер неактивен, шина OT в покое */

    /* RX pin — вход с прерыванием по любому фронту */
    gpio_config_t rx_cfg = {
        .pin_bit_mask = 1ULL << GPIO_OT_RX,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        /* ANYEDGE: ISR нужен на оба фронта для edge-based декодирования битов.
         * Ложные срабатывания в OT_S_TX и OT_S_WAIT_RX (NEGEDGE) игнорируются
         * в ISR: в WAIT_RX реагируем только если level==1 (POSEDGE). */
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&rx_cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_OT_RX, ot_rx_isr, NULL);

    /* Семафор для ожидания завершения транзакции */
    ot_done_sem = xSemaphoreCreateBinary();

    /* esp_timer — 500 мкс периодический */
    const esp_timer_create_args_t timer_args = {
        .callback  = ot_timer_cb,
        .arg       = NULL,
        .dispatch_method = ESP_TIMER_ISR,   /* ISR-контекст для минимальной задержки */
        .name      = "opentherm",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ot_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(ot_timer_handle, 500)); /* 500 мкс */

    ESP_LOGI(TAG, "OpenTherm инициализирован (TX=GPIO%d, RX=GPIO%d)", GPIO_OT_TX, GPIO_OT_RX);
    /* SmartTherm: idle = GPIO LOW (0), если котёл включён */
    ESP_LOGI(TAG, "RX idle level = %d  (ожидается 0 если котёл подключён и включён)", OT_RX_READ());
}

/* Одна транзакция мастер → котёл → мастер.
   Возвращает true если ответ получен и корректен. */
static bool ot_transaction(const OT_Frame *req, OT_Frame *rsp)
{
    /* Дождаться idle */
    uint32_t t0 = xTaskGetTickCount();
    while (ot_state != OT_S_IDLE) {
        if (xTaskGetTickCount() - t0 > pdMS_TO_TICKS(200)) return false;
        vTaskDelay(1);
    }

    /* ── Отладка перед TX ── */
    int rx_idle = OT_RX_READ();
    uint64_t tx_frame_dbg = build_frame(req);
    ESP_LOGI(TAG, ">>> TX ID=%d type=%d frame=0x%09llX  RX_idle=%d  ISR=%lu TX_done=%lu RX_done=%lu TMOUT=%lu",
             req->data_id, req->msg_type, tx_frame_dbg,
             rx_idle, dbg_isr_count, dbg_tx_done, dbg_rx_done, dbg_timeout);

    /* Подготовить фрейм */
    ot_tx_frame = tx_frame_dbg;
    ot_tx_idx   = 0;
    ot_tx_half  = 0;
    ot_state    = OT_S_TX;

    /* Ждать завершения (семафор выдаётся из ISR) */
    if (xSemaphoreTake(ot_done_sem, pdMS_TO_TICKS(OT_RESPONSE_TIMEOUT_MS + 200))
            != pdTRUE) {
        ot_state = OT_S_IDLE;
        ESP_LOGW(TAG, "    TIMEOUT ID=%d  ISR=%lu TX_done=%lu",
                 req->data_id, dbg_isr_count, dbg_tx_done);
        return false;
    }

    if (ot_state == OT_S_ERROR) {
        ot_state = OT_S_IDLE;
        ESP_LOGW(TAG, "    NO_RESP ID=%d  ISR=%lu TX_done=%lu  last_raw=0x%09llX",
                 req->data_id, dbg_isr_count, dbg_tx_done, dbg_last_raw);
        return false;
    }

    uint64_t raw = ot_rx_frame;
    parse_frame(raw, rsp);
    ot_state = OT_S_IDLE;

    ESP_LOGI(TAG, "    <<< ID=%d raw=0x%09llX rsp_type=%d rsp_id=%d val=0x%04X",
             req->data_id, raw, rsp->msg_type, rsp->data_id, rsp->data_value);

    /* Проверка: ID ответа должен совпадать */
    if (rsp->data_id != req->data_id) {
        ESP_LOGW(TAG, "    ID mismatch: sent %d got %d", req->data_id, rsp->data_id);
        return false;
    }

    return true;
}

/* ── Инициализация OpenTherm сессии с котлом ───────────────────────────────
 *
 * OTGateway делает это при подключении и каждые 60 минут:
 *   READ  ID=125 (SlaveVersion)
 *   WRITE ID=126 (MasterVersion)
 *   READ  ID=3   (SlaveConfig)
 *   WRITE ID=2   (MasterConfig — эхо slave member ID)
 *
 * Без этого некоторые котлы Baxi не разрешают DHW управление.
 */
static bool     ot_initialized = false;
static uint32_t ot_init_time_ms = 0;
#define OT_REINIT_INTERVAL_MS 3600000  /* 60 минут */

static OT_State *hs_state = NULL;  /* для сохранения версий из handshake */

static void ot_handshake(void)
{
    OT_Frame req = {0}, rsp = {0};
    bool ok;

    /* READ ID=125 — версия ПО котла */
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_SLAVE_VERSION;
    req.data_value = 0;
    ok = ot_transaction(&req, &rsp);
    if (ok && hs_state) {
        hs_state->slave_type    = (uint8_t)(rsp.data_value >> 8);
        hs_state->slave_version = (uint8_t)(rsp.data_value & 0xFF);
    }
    ESP_LOGI(TAG, "Handshake: SlaveVersion ok=%d val=0x%04X", ok, rsp.data_value);

    /* WRITE ID=126 — версия ПО мастера (type=1, version=0x3F как в OTGateway) */
    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    req.msg_type = OT_MSG_WRITE_DATA;
    req.data_id  = OT_ID_MASTER_VERSION;
    req.data_value = 0x013F;  /* type=0x01, version=0x3F */
    ok = ot_transaction(&req, &rsp);
    ESP_LOGI(TAG, "Handshake: MasterVersion ok=%d", ok);

    /* READ ID=3 — конфигурация котла (DHW present, storage type, CH2 present) */
    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_SLAVE_CONFIG;
    req.data_value = 0;
    ok = ot_transaction(&req, &rsp);
    if (ok) {
        uint8_t flags = (uint8_t)(rsp.data_value >> 8);
        uint8_t member_id = (uint8_t)(rsp.data_value & 0xFF);
        ESP_LOGI(TAG, "Handshake: SlaveConfig flags=0x%02X memberId=%d"
                 " DHW=%d Storage=%d CH2=%d",
                 flags, member_id,
                 (flags >> 0) & 1,   /* bit 0: DHW present */
                 (flags >> 3) & 1,   /* bit 3: DHW storage (vs instantaneous) */
                 (flags >> 5) & 1);  /* bit 5: CH2 present */

        /* WRITE ID=2 — эхо slave member ID (как делает OTGateway) */
        memset(&req, 0, sizeof(req));
        memset(&rsp, 0, sizeof(rsp));
        req.msg_type = OT_MSG_WRITE_DATA;
        req.data_id  = OT_ID_MASTER_CONFIG;
        req.data_value = (uint16_t)((flags << 8) | member_id);
        ot_transaction(&req, &rsp);
        ESP_LOGI(TAG, "Handshake: MasterConfig written flags=0x%02X id=%d", flags, member_id);
    }

    /* READ ID=124 — версия протокола OT котла */
    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_OT_VERSION_S;
    req.data_value = 0;
    ok = ot_transaction(&req, &rsp);
    if (ok && hs_state)
        hs_state->ot_version = OT_f88_to_float(rsp.data_value);
    ESP_LOGI(TAG, "Handshake: OT Version ok=%d val=0x%04X", ok, rsp.data_value);

    ot_initialized = true;
    ot_init_time_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "Handshake complete");
}

/* ── Последовательность опроса котла ────────────────────────────────────────
 *
 * Конфигурация: Baxi Duo-tec Compact 1.24 + бойлер косвенного нагрева (БКН)
 *
 * DHW работает через стандартный механизм: DHW_ENABLE + ID=56 (DHW setpoint).
 * CH2 НЕ поддерживается этим котлом (ID=8 → UNKNOWN_DATA_ID).
 * Для работы DHW обязателен handshake (ID=125/126/3/2) при старте.
 *
 * ВАЖНО: STATUS (ID=0) отправляется КАЖДЫЙ цикл опроса (~1 сек).
 */
#define POLL_EXTRA_STEPS 11  /* количество доп. запросов (шаги 0..10) */
static int poll_extra = 0;

/* Отправка STATUS (ID=0) — вызывается каждый цикл */
static void poll_status(OT_State *s)
{
    OT_Frame req = {0}, rsp = {0};
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_STATUS;
    {
        uint8_t m = 0;
        /* CH разрешён пользователем И приоритет БКН не активен */
        if (s->ch_enable && !s->dhw_priority) m |= OT_MASTER_CH_ENABLE;
        if (s->dhw_enable) m |= OT_MASTER_DHW_ENABLE;
        if (s->dhw_enable) m |= OT_MASTER_CH2_ENABLE;  /* CH2 = нагрев бойлера КН (косвенный) */
        /* DHW Blocking: стандартный OT-сигнал — просим котёл удерживать CH пока греется БКН */
        if (s->dhw_priority)  m |= OT_MASTER_DHW_BLOCK;
        uint8_t lb = 0;
        if (s->fault_reset) lb = 1;  /* бит 0 LB = сброс аварии */
        req.data_value = (uint16_t)((m << 8) | lb);
    }
    bool ok = ot_transaction(&req, &rsp);
    if (s->fault_reset) s->fault_reset = false;  /* однократный сброс */
    if (ok) {
        uint8_t sl = (uint8_t)(rsp.data_value & 0xFF);
        s->fault      = (sl & OT_SLAVE_FAULT)     != 0;
        s->ch_active  = (sl & OT_SLAVE_CH_ACTIVE) != 0;
        s->dhw_active = (sl & OT_SLAVE_DHW_ACTIVE)!= 0;
        s->flame      = (sl & OT_SLAVE_FLAME)     != 0;
        s->connected  = true;
        s->last_response_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
}

void OT_Poll(OT_State *s)
{
    OT_Frame req = {0}, rsp = {0};
    bool ok;

    /* ── Handshake при старте и каждые 60 минут ── */
    {
        hs_state = s;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (!ot_initialized || (now_ms - ot_init_time_ms > OT_REINIT_INTERVAL_MS)) {
            ot_handshake();
        }
    }

    /* ── Приоритет нагрева бойлера КН с гистерезисом ──────────────────────────
     * Управление только по температуре датчика котла (ID=26).
     * Флаг dhw_active намеренно не используется: реакция на него у уставки
     * приводила к быстрой осцилляции (котёл ещё показывал dhw_active=1
     * при 54.9°C сразу после снятия приоритета на 55°C).
     *
     * ON:  dhw_temp < setpoint - DHW_HYST_ON  (temp < 53°C при setpoint=55°C)
     * OFF: dhw_temp >= setpoint               (temp >= 55°C)
     *
     * Зоны не перекрываются → осцилляция исключена.
     */
    if (s->dhw_enable && s->dhw_temp > 5.0f) {
        if (!s->dhw_priority && s->dhw_temp < s->dhw_setpoint - DHW_HYST_ON) {
            s->dhw_priority = true;
            ESP_LOGI(TAG, "DHW priority ON:  temp=%.1f  setpoint=%.1f  (CH подавлен)",
                     (double)s->dhw_temp, (double)s->dhw_setpoint);
        } else if (s->dhw_priority && s->dhw_temp >= s->dhw_setpoint) {
            s->dhw_priority = false;
            ESP_LOGI(TAG, "DHW priority OFF: temp=%.1f  setpoint=%.1f  (CH восстановлен)",
                     (double)s->dhw_temp, (double)s->dhw_setpoint);
        }
    } else {
        s->dhw_priority = false;
    }

    /* ── Всегда сначала STATUS ── */
    poll_status(s);

    /* ── Затем один дополнительный запрос по кругу ── */
    switch (poll_extra) {

    /* ── Максимальная уставка CH (ID=57) — ПЕРВЫМ, иначе котёл зажимает CH setpoint ── */
    case 0:
        req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_MAX_CH_SETPOINT;
        req.data_value = OT_float_to_f88(80.0f);
        ot_transaction(&req, &rsp);
        break;

    /* ── Уставка CH ── */
    case 1: {
            req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_CH_SETPOINT;
        req.data_value = OT_float_to_f88(s->ch_setpoint);
        ot_transaction(&req, &rsp);
        break;
    }

    /* ── Модуляция ── */
    case 2:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_MODULATION;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK)
            s->modulation = OT_f88_to_float(rsp.data_value);
        break;

    /* ── Температура подачи CH ── */
    case 3:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_CH_TEMP;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK)
            s->ch_temp = OT_f88_to_float(rsp.data_value);
        break;

    /* ── Температура БКН (датчик NTC в баке, ID=26) ── */
    case 4:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_DHW_TEMP;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK)
            s->dhw_temp = OT_f88_to_float(rsp.data_value);
        break;

    /* ── Температура обратки ── */
    case 5:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_RETURN_TEMP;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK)
            s->return_temp = OT_f88_to_float(rsp.data_value);
        break;

    /* ── Уставка ГВС (DHW, ID=56) ── */
    case 6:
        req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_DHW_SETPOINT;
        req.data_value = OT_float_to_f88(s->dhw_setpoint);
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_WRITE_ACK)
            s->dhw_setpoint = OT_f88_to_float(rsp.data_value);
        break;

    /* ── Границы уставки БКН (ID=48) ── */
    case 7:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_DHW_BOUNDS;
        req.data_value = 0;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float hi = (float)((rsp.data_value >> 8) & 0xFF);
            float lo = (float)((rsp.data_value     ) & 0xFF);
            if (hi > lo && hi > 0) {
                s->dhw_setpoint_max = hi;
                s->dhw_setpoint_min = lo;
                if (s->dhw_setpoint > hi) s->dhw_setpoint = hi;
                if (s->dhw_setpoint < lo) s->dhw_setpoint = lo;
            }
        }
        break;

    /* ── Границы уставки CH (ID=49) ── */
    case 8:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_CH_BOUNDS;
        req.data_value = 0;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float hi = (float)((rsp.data_value >> 8) & 0xFF);
            float lo = (float)((rsp.data_value     ) & 0xFF);
            if (hi > lo && hi > 0) {
                s->ch_setpoint_max = hi;
                s->ch_setpoint_min = lo;
            }
        }
        break;

    /* ── Коды ошибок (ID=5) ── */
    case 9:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_ASF_FLAGS;
        req.data_value = 0;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            s->asf_flags      = (uint8_t)(rsp.data_value >> 8);
            s->oem_fault_code = (uint8_t)(rsp.data_value & 0xFF);
        }
        break;

    /* ── OEM диагностика (ID=115) ── */
    case 10:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_OEM_DIAGNOSTIC;
        req.data_value = 0;
        ok = ot_transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK)
            s->oem_diagnostic = rsp.data_value;
        break;

    }

    poll_extra++;
    if (poll_extra >= POLL_EXTRA_STEPS) poll_extra = 0;

    /* Проверить таймаут связи */
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - s->last_response_ms > 10000) {
            s->connected = false;
            ESP_LOGW(TAG, "Котёл не отвечает > 10 сек");
        }
    }
}
