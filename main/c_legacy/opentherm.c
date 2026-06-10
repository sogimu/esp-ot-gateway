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

/* Forward declaration for ot_transaction (defined below) */
static bool ot_transaction(const OT_Frame *req, OT_Frame *rsp);

bool OT_Transaction(const OT_Frame *request, OT_Frame *response)
{
    return ot_transaction(request, response);
}

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

    /* Реле — выход, HIGH = замкнут (NO контакт) */
    gpio_config_t relay_cfg = {
        .pin_bit_mask = 1ULL << GPIO_RELAY,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&relay_cfg);
    gpio_set_level(GPIO_RELAY, 1);   /* замкнуть при старте */

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

/* ── Читаемое логирование OT-сообщений ──────────────────────────────────── */

static const char *ot_id_name(uint8_t id)
{
    switch (id) {
    case 0:   return "STATUS";
    case 1:   return "CH_sp";
    case 2:   return "MasterCfg";
    case 3:   return "SlaveCfg";
    case 5:   return "Faults";
    case 17:  return "Modulation";
    case 25:  return "CH_temp";
    case 26:  return "DHW_temp";
    case 28:  return "Return";
    case 48:  return "DHW_bounds";
    case 49:  return "CH_bounds";
    case 56:  return "DHW_sp";
    case 57:  return "MaxCH_sp";
    case 115: return "OEM_diag";
    case 124: return "OT_ver";
    case 125: return "Slave_ver";
    case 126: return "Master_ver";
    default:  return "?";
    }
}

static const char *ot_msg_name(uint8_t type)
{
    switch (type) {
    case OT_MSG_READ_DATA:    return "READ";
    case OT_MSG_WRITE_DATA:   return "WRITE";
    case OT_MSG_READ_ACK:     return "READ_ACK";
    case OT_MSG_WRITE_ACK:    return "WRITE_ACK";
    case OT_MSG_DATA_INVALID: return "INVALID";
    case OT_MSG_UNKNOWN_ID:   return "UNKNOWN";
    default:                  return "?";
    }
}

/* Формат значения для лога (двузначное — используется в TX и RX) */
static void ot_fmt_val(char *buf, size_t len, uint8_t id, uint16_t val)
{
    switch (id) {
    case 1:  case 56: case 57:          /* setpoints */
    case 25: case 26: case 28: case 124:/* temperatures, OT version */
        snprintf(buf, len, "%.1f°C", (double)OT_f88_to_float(val));
        break;
    case 17:                             /* modulation */
        snprintf(buf, len, "%.1f%%", (double)OT_f88_to_float(val));
        break;
    case 0:  case 2: case 3: case 5:
    case 48: case 49: case 115:
    case 125: case 126:
        snprintf(buf, len, "0x%04X", val);
        break;
    default:
        snprintf(buf, len, "%u", val);
        break;
    }
}

/* Формат мастер-байта STATUS (ID=0, HB) для TX-лога */
static void ot_fmt_status_master(char *buf, size_t len, uint8_t m)
{
    int pos = 0;
    if (m & OT_MASTER_CH_ENABLE)  pos += snprintf(buf + pos, len - pos, " CH=1");
    else                          pos += snprintf(buf + pos, len - pos, " CH=0");
    if (m & OT_MASTER_DHW_ENABLE) pos += snprintf(buf + pos, len - pos, " DHW=1");
    else                          pos += snprintf(buf + pos, len - pos, " DHW=0");
    if (m & OT_MASTER_CH2_ENABLE) pos += snprintf(buf + pos, len - pos, " CH2=1");
    if (m & OT_MASTER_DHW_BLOCK)  pos += snprintf(buf + pos, len - pos, " BLOCK=1");
    (void)len;
}

/* Формат слейв-байта STATUS (ID=0, LB) для RX-лога */
static void ot_fmt_status_slave(char *buf, size_t len, uint8_t sl)
{
    int pos = 0;
    pos += snprintf(buf + pos, len - pos, " fault=%d", (sl >> 0) & 1);
    pos += snprintf(buf + pos, len - pos, " ch=%d",   (sl >> 1) & 1);
    pos += snprintf(buf + pos, len - pos, " dhw=%d",  (sl >> 2) & 1);
    pos += snprintf(buf + pos, len - pos, " flame=%d",(sl >> 3) & 1);
    (void)len;
}

/* Одна транзакция мастер → котёл → мастер.
   Возвращает true если ответ получен и корректен.
   Гарантирует межфреймовую паузу ≥ 100 мс (требование спецификации OpenTherm). */
static bool ot_transaction(const OT_Frame *req, OT_Frame *rsp)
{
    /* Межфреймовая пауза: ≥ 100 мс от конца предыдущего ответа */
    static uint32_t last_txn_ms = 0;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    int32_t wait = OT_MIN_GAP_MS - (int32_t)(now_ms - last_txn_ms);
    if (wait > 0) vTaskDelay(pdMS_TO_TICKS(wait));

    /* Дождаться idle */
    uint32_t t0 = xTaskGetTickCount();
    while (ot_state != OT_S_IDLE) {
        if (xTaskGetTickCount() - t0 > pdMS_TO_TICKS(200)) return false;
        vTaskDelay(1);
    }

    /* ── TX ── */
    uint64_t tx_frame = build_frame(req);

    /* ── Подготовить фрейм ── */
    ot_tx_frame = tx_frame;
    ot_tx_idx   = 0;
    ot_tx_half  = 0;
    ot_state    = OT_S_TX;

    /* Ждать завершения (семафор выдаётся из ISR) */
    if (xSemaphoreTake(ot_done_sem, pdMS_TO_TICKS(OT_RESPONSE_TIMEOUT_MS + 200))
            != pdTRUE) {
        ot_state = OT_S_IDLE;
        last_txn_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGW(TAG, "OT %-5s %-10s(%d)  |  TIMEOUT",
                 ot_msg_name(req->msg_type), ot_id_name(req->data_id), req->data_id);
        return false;
    }

    if (ot_state == OT_S_ERROR) {
        ot_state = OT_S_IDLE;
        last_txn_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGW(TAG, "OT %-5s %-10s(%d)  |  NO_RESP",
                 ot_msg_name(req->msg_type), ot_id_name(req->data_id), req->data_id);
        return false;
    }

    uint64_t raw = ot_rx_frame;
    parse_frame(raw, rsp);
    ot_state = OT_S_IDLE;

    last_txn_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* ── Лог: TX → RX одним сообщением ── */
    {
        char tx_val[32] = "";
        char rx_val[64] = "";
        uint8_t id = req->data_id;
        uint16_t txv = req->data_value;

        if (id == 0) {
            ot_fmt_status_master(tx_val, sizeof(tx_val), (uint8_t)(txv >> 8));
        } else if (req->msg_type == OT_MSG_WRITE_DATA) {
            ot_fmt_val(tx_val, sizeof(tx_val), id, txv);
            memmove(tx_val + 2, tx_val, strlen(tx_val) + 1);
            tx_val[0] = '='; tx_val[1] = ' ';
        }

        if (rsp->data_id == 0 && rsp->msg_type == OT_MSG_READ_ACK) {
            ot_fmt_status_slave(rx_val, sizeof(rx_val), (uint8_t)(rsp->data_value & 0xFF));
        } else if (rsp->msg_type == OT_MSG_WRITE_ACK || rsp->msg_type == OT_MSG_READ_ACK) {
            ot_fmt_val(rx_val, sizeof(rx_val), rsp->data_id, rsp->data_value);
            memmove(rx_val + 2, rx_val, strlen(rx_val) + 1);
            rx_val[0] = '='; rx_val[1] = ' ';
        } else {
            snprintf(rx_val, sizeof(rx_val), "%s", ot_msg_name(rsp->msg_type));
        }

        ESP_LOGI(TAG, "OT %-5s %-10s(%d) %-24s -> %s",
                 ot_msg_name(req->msg_type), ot_id_name(id), id,
                 tx_val, rx_val);
    }

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
#define POLL_EXTRA_STEPS 18  /* количество доп. запросов (шаги 0..17, приоритетный цикл) */
static int poll_extra = 0;

/* Отправка STATUS (ID=0) — вызывается каждый цикл */
static void poll_status(OT_State *s)
{
    OT_Frame req = {0}, rsp = {0};
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_STATUS;
    {
        uint8_t m = 0;
        /* CH всегда разрешён когда пользователь его включил.
         * DHW разрешён только когда dhw_priority=true (гистерезис:
         * БКН ниже уставки → греем, достиг уставки → запрещаем).
         * CH2 необходим котлу для переключения 3-ход. клапана на БКН,
         * несмотря на SlaveConfig CH2=0. */
        if (s->ch_enable)  m |= OT_MASTER_CH_ENABLE;
        if (s->dhw_enable && s->dhw_priority) m |= OT_MASTER_DHW_ENABLE;
        if (s->dhw_enable && s->dhw_priority) m |= OT_MASTER_CH2_ENABLE;
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
