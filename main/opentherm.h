#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * OpenTherm master для ESP32 + SmartTherm adapter
 *
 * Подключение (SmartTherm, раздел 8.2.1 инструкции):
 *   GPIO_OT_TX (GPIO 4)  — выход к адаптеру (OpenTherm OUT = D4)
 *   GPIO_OT_RX (GPIO 16) — вход от адаптера (OpenTherm IN  = RX2)
 *
 * Полярность сигналов адаптера SmartTherm (ihormelnyk/opentherm_library):
 *   TX: GPIO LOW  → active (ток через шину OT)
 *       GPIO HIGH → idle   (шина OT в покое)
 *   RX: шина active → GPIO HIGH
 *       шина idle   → GPIO LOW
 *
 * Таймер: esp_timer (high-resolution), период 500 мкс (полубит Manchester)
 */
#define GPIO_OT_TX   4
#define GPIO_OT_RX   16

/* OpenTherm Data IDs */
#define OT_ID_STATUS            0
#define OT_ID_CH_SETPOINT       1
#define OT_ID_MODULATION        17
#define OT_ID_CH_PRESSURE       18
/* ID 19 (DHW flow) не используется — бойлер КН без расходомера */
#define OT_ID_CH_TEMP           25
#define OT_ID_DHW_TEMP          26   /* температура бойлера КН (датчик NTC в баке) */
#define OT_ID_OUTSIDE_TEMP      27
#define OT_ID_RETURN_TEMP       28
#define OT_ID_DHW_SETPOINT      56   /* уставка бойлера КН */
#define OT_ID_MAX_CH_SETPOINT   57
#define OT_ID_SLAVE_CONFIG      3    /* конфигурация котла (read) */
#define OT_ID_MASTER_CONFIG     2    /* конфигурация мастера (write) */
#define OT_ID_CH2_SETPOINT      8    /* уставка CH2 — используется для БКН на системных котлах */
#define OT_ID_DHW_BOUNDS        48   /* границы допустимой уставки ГВС (lo/hi байты) */
#define OT_ID_CH_BOUNDS         49   /* границы допустимой уставки CH (lo/hi байты) */
#define OT_ID_ASF_FLAGS          5   /* коды ошибок: HB=flags, LB=OEM code */
#define OT_ID_OEM_DIAGNOSTIC   115   /* OEM диагностический код */
#define OT_ID_BURNER_STARTS    116   /* счётчик запусков горелки */
#define OT_ID_CH_PUMP_STARTS   117   /* счётчик запусков насоса CH */
#define OT_ID_DHW_VALVE_STARTS 118   /* счётчик переключений 3-ход. клапана */
#define OT_ID_DHW_BURNER_STARTS 119  /* счётчик запусков горелки для DHW */
#define OT_ID_BURNER_HOURS     120   /* наработка горелки (часы) */
#define OT_ID_CH_PUMP_HOURS    121   /* наработка насоса CH (часы) */
#define OT_ID_DHW_VALVE_HOURS  122   /* наработка клапана DHW (часы) */
#define OT_ID_DHW_BURNER_HOURS 123   /* наработка горелки DHW (часы) */
#define OT_ID_OT_VERSION_S     124   /* версия протокола OT котла (read) */
#define OT_ID_SLAVE_VERSION    125   /* версия ПО котла (read) */
#define OT_ID_MASTER_VERSION   126   /* версия ПО мастера (write) */

/* Типы сообщений */
#define OT_MSG_READ_DATA   0x00
#define OT_MSG_WRITE_DATA  0x01
#define OT_MSG_READ_ACK    0x04
#define OT_MSG_WRITE_ACK   0x05
#define OT_MSG_DATA_INVALID 0x06
#define OT_MSG_UNKNOWN_ID  0x07

/* Флаги статуса — мастер (старший байт ID=0) */
#define OT_MASTER_CH_ENABLE   (1 << 0)
#define OT_MASTER_DHW_ENABLE  (1 << 1)
#define OT_MASTER_CH2_ENABLE  (1 << 4)
#define OT_MASTER_DHW_BLOCK   (1 << 6)  /* DHW Blocking: просим котёл удерживать CH пока активен DHW */

/* Флаги статуса — котёл (младший байт ID=0) */
#define OT_SLAVE_FAULT      (1 << 0)
#define OT_SLAVE_CH_ACTIVE  (1 << 1)
#define OT_SLAVE_DHW_ACTIVE (1 << 2)
#define OT_SLAVE_FLAME      (1 << 3)

#define OT_RESPONSE_TIMEOUT_MS  800
#define OT_POLL_INTERVAL_MS    1000

/*
 * Гистерезис приоритета нагрева бойлера КН (°C)
 *
 * ID=26 — это датчик самого котла. Котёл останавливает нагрев БКН
 * когда его датчик достигает dhw_setpoint. Мы используем тот же датчик.
 *
 * Когда dhw_temp опускается ниже (dhw_setpoint - DHW_HYST_ON):
 *   → CH временно отключается, котёл греет только БКН.
 * Когда dhw_temp достигает dhw_setpoint:
 *   → CH восстанавливается (котёл в этот момент сам завершает нагрев БКН).
 *
 * Пример при setpoint=55°C:
 *   приоритет ON  когда temp < 53°C
 *   приоритет OFF когда temp >= 55°C
 *
 * Управление только по температуре, без реакции на флаг dhw_active —
 * это исключает осцилляцию у уставки.
 */
#define DHW_HYST_ON   2.0f   /* включить приоритет: упало на 2°C ниже уставки   */

/* Фрейм OpenTherm */
typedef struct {
    uint8_t  msg_type;
    uint8_t  data_id;
    uint16_t data_value;
} OT_Frame;

/*
 * Состояние котла Baxi Duo-tec Compact 1.24
 *
 * Конфигурация: системный котёл + бойлер косвенного нагрева (БКН)
 * 3-ходовой клапан управляется котлом автоматически:
 *   dhw_active=false → клапан на отопление (CH)
 *   dhw_active=true  → клапан на змеевик БКН (DHW)
 * Решение о переключении котёл принимает самостоятельно:
 *   если dhw_enable=1 И t_бойлера < dhw_setpoint → переключить на БКН
 */
typedef struct {
    bool     connected;
    uint32_t last_response_ms;

    /* Статус горелки и клапана */
    bool     fault;
    bool     ch_active;       /* насос CH работает */
    bool     dhw_active;      /* 3-ход. клапан → БКН, идёт нагрев бойлера */
    bool     flame;

    /* Температуры */
    float    ch_temp;         /* подача первичного контура */
    float    return_temp;     /* обратка первичного контура */
    float    dhw_temp;        /* температура в баке БКН (датчик NTC) */
    float    outside_temp;

    /* Параметры работы */
    float    modulation;      /* % мощности горелки */

    /* Уставки */
    float    ch_setpoint;     /* уставка температуры подачи CH */
    float    dhw_setpoint;    /* уставка температуры БКН */
    float    dhw_setpoint_min; /* нижняя граница уставки БКН (от котла) */
    float    dhw_setpoint_max; /* верхняя граница уставки БКН (от котла) */
    float    ch_setpoint_min;  /* нижняя граница уставки CH (от котла) */
    float    ch_setpoint_max;  /* верхняя граница уставки CH (от котла) */

    /* Коды ошибок */
    uint8_t  asf_flags;       /* ID=5 HB: bit0=service, bit1=lockout, bit2=low press, bit3=gas, bit4=air, bit5=overtemp */
    uint8_t  oem_fault_code;  /* ID=5 LB: OEM-код ошибки */
    uint16_t oem_diagnostic;  /* ID=115: OEM диагностический код */

    /* Наработка */
    uint16_t burner_starts;   /* ID=116 */
    uint16_t ch_pump_starts;  /* ID=117 */
    uint16_t dhw_valve_starts;/* ID=118 */
    uint16_t dhw_burner_starts;/* ID=119 */
    uint16_t burner_hours;    /* ID=120 */
    uint16_t ch_pump_hours;   /* ID=121 */
    uint16_t dhw_valve_hours; /* ID=122 */
    uint16_t dhw_burner_hours;/* ID=123 */

    /* Версии */
    uint8_t  slave_type;      /* ID=125 HB */
    uint8_t  slave_version;   /* ID=125 LB */
    float    ot_version;      /* ID=124 f8.8 */

    /* Управление */
    bool     ch_enable;
    bool     dhw_enable;      /* разрешить нагрев БКН */
    bool     fault_reset;     /* однократный сброс аварии (через LB STATUS) */
    bool     dhw_priority;    /* приоритет БКН активен (CH временно подавлен) */
} OT_State;

/* Инициализация (GPIO + esp_timer) */
void OT_Init(void);

/* Опрос котла — вызывать каждые OT_POLL_INTERVAL_MS мс */
void OT_Poll(OT_State *state);

/* Вспомогательные функции формата f8.8 */
float    OT_f88_to_float(uint16_t v);
uint16_t OT_float_to_f88(float f);
