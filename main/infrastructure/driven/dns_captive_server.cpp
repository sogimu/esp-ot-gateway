#include "infrastructure/driven/dns_captive_server.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

const char* DnsCaptiveServer::TAG = "dns_captive";

void DnsCaptiveServer::start()
{
    start_called_ = true;
    if (running_) return;

    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "Не удалось создать UDP-сокет");
        return;
    }

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Не удалось занять порт 53");
        close(sock_);
        sock_ = -1;
        return;
    }

    running_ = true;
    xTaskCreate(dns_task, "dns_captive", 3072, this, 1, (TaskHandle_t*)&task_);
    ESP_LOGI(TAG, "DNS-сервер запущен (192.168.4.1)");
}

void DnsCaptiveServer::stop()
{
    if (!running_) return;
    running_ = false;
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    // Task will exit on running_==false; give it a moment
    vTaskDelay(pdMS_TO_TICKS(100));
    task_ = nullptr;
    ESP_LOGI(TAG, "DNS-сервер остановлен");
}

void DnsCaptiveServer::dns_task(void* arg)
{
    auto* self = static_cast<DnsCaptiveServer*>(arg);
    uint8_t buf[512];

    while (self->running_) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int len = recvfrom(self->sock_, buf, sizeof(buf), 0,
                          (struct sockaddr*)&client, &client_len);
        if (len < 12) continue;  // too short, ignore

        // Preserve transaction ID
        // uint16_t txid = (buf[0] << 8) | buf[1];  // unchanged

        // Build response header
        buf[2]  = 0x81; buf[3]  = 0x80;  // flags: response, no error
        buf[4]  = 0x00; buf[5]  = 0x01;  // QDCOUNT = 1 (copy from request)
        buf[6]  = 0x00; buf[7]  = 0x01;  // ANCOUNT = 1
        buf[8]  = 0x00; buf[9]  = 0x00;  // NSCOUNT = 0
        buf[10] = 0x00; buf[11] = 0x00;  // ARCOUNT = 0

        // Find end of question section
        int qend = 12;
        while (qend < len && buf[qend] != 0x00) qend++;
        qend += 5;  // skip null terminator + QTYPE(2) + QCLASS(2)

        // Answer section: 192.168.4.1
        buf[qend + 0]  = 0xC0; buf[qend + 1]  = 0x0C;  // name pointer
        buf[qend + 2]  = 0x00; buf[qend + 3]  = 0x01;  // TYPE A
        buf[qend + 4]  = 0x00; buf[qend + 5]  = 0x01;  // CLASS IN
        buf[qend + 6]  = 0x00; buf[qend + 7]  = 0x00;
        buf[qend + 8]  = 0x00; buf[qend + 9]  = 0x3C;  // TTL = 60
        buf[qend + 10] = 0x00; buf[qend + 11] = 0x04;  // RDLENGTH = 4
        buf[qend + 12] = 192;  buf[qend + 13] = 168;   // 192.168
        buf[qend + 14] = 4;    buf[qend + 15] = 1;     // 4.1

        sendto(self->sock_, buf, qend + 16, 0,
               (struct sockaddr*)&client, client_len);
    }

    vTaskDelete(nullptr);
}
