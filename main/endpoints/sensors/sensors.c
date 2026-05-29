#include "sensors.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "sensor";

float sensor1_temp = -127.0f;
float sensor2_temp = -127.0f;

/* ── Low-level OneWire via GPIO bit-bang ──────────────────────────────────── */

static void ow_drive_low(int pin)
{
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

static void ow_release(int pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

static bool ow_reset(int pin)
{
    ow_drive_low(pin);
    esp_rom_delay_us(480);
    ow_release(pin);
    esp_rom_delay_us(70);
    bool present = (gpio_get_level(pin) == 0);
    esp_rom_delay_us(410);
    return present;
}

static void ow_write_bit(int pin, int bit)
{
    ow_drive_low(pin);
    if (bit) {
        esp_rom_delay_us(5);
        ow_release(pin);
        esp_rom_delay_us(65);
    } else {
        esp_rom_delay_us(65);
        ow_release(pin);
        esp_rom_delay_us(5);
    }
}

static int ow_read_bit(int pin)
{
    ow_drive_low(pin);
    esp_rom_delay_us(3);
    ow_release(pin);
    esp_rom_delay_us(8);
    int bit = gpio_get_level(pin) ? 1 : 0;
    esp_rom_delay_us(55);
    return bit;
}

static void ow_write_byte(int pin, uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(pin, b & 1);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(int pin)
{
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        b >>= 1;
        if (ow_read_bit(pin)) b |= 0x80;
    }
    return b;
}

/* ── DS18B20 helpers ──────────────────────────────────────────────────────── */

static bool ds18b20_start_convert(int pin)
{
    if (!ow_reset(pin)) return false;
    ow_write_byte(pin, 0xCC);
    ow_write_byte(pin, 0x44);
    return true;
}

static bool ds18b20_read_temp(int pin, float *out)
{
    *out = -127.0f;
    if (!ow_reset(pin)) return false;
    ow_write_byte(pin, 0xCC);
    ow_write_byte(pin, 0xBE);
    uint8_t sp[9];
    for (int i = 0; i < 9; i++) sp[i] = ow_read_byte(pin);
    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t b = sp[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ b) & 1;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    if (crc != sp[8]) return false;
    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    *out = (float)raw * 0.0625f;
    return true;
}

/* ── Initialization ────────────────────────────────────────────────────────── */

void sensors_init(void)
{
    gpio_reset_pin(SENSOR_T1_GPIO);
    gpio_reset_pin(SENSOR_T2_GPIO);

    /* Первая проверка presence (ещё до старта цикла опроса) */
    bool p1 = ow_reset(SENSOR_T1_GPIO);
    bool p2 = ow_reset(SENSOR_T2_GPIO);
    ESP_LOGI(TAG, "Датчики: T1=GPIO%d%s T2=GPIO%d%s",
             SENSOR_T1_GPIO, p1 ? " (OK)" : " (нет)",
             SENSOR_T2_GPIO, p2 ? " (OK)" : " (нет)");
}

/* ── Non-blocking poll (state machine, runs every ~5 cycles) ───────────────── */

static int s_skip = 5;
static bool s_converting = false;

void sensors_poll(void)
{
    s_skip++;
    if (s_skip < 5) return;
    s_skip = 0;

    if (!s_converting) {
        bool ok1 = ds18b20_start_convert(SENSOR_T1_GPIO);
        bool ok2 = ds18b20_start_convert(SENSOR_T2_GPIO);
        if (!ok1) ESP_LOGW(TAG, "T1 start conv failed");
        if (!ok2) ESP_LOGW(TAG, "T2 start conv failed");
        s_converting = ok1 || ok2;
    } else {
        float t;
        if (ds18b20_read_temp(SENSOR_T1_GPIO, &t)) {
            sensor1_temp = t;
            ESP_LOGI(TAG, "T1=%.1f°C", (double)t);
        } else {
            ESP_LOGW(TAG, "T1 read failed");
        }
        if (ds18b20_read_temp(SENSOR_T2_GPIO, &t)) {
            sensor2_temp = t;
            ESP_LOGI(TAG, "T2=%.1f°C", (double)t);
        } else {
            ESP_LOGW(TAG, "T2 read failed");
        }
        s_converting = false;
    }
}