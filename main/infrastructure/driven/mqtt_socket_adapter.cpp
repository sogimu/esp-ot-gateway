#include "infrastructure/driven/mqtt_socket_adapter.h"

#include "lwip/sockets.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>

static void force_close_socket(int& sock);

static const char* TAG = "mqtt_sock";

// ── MQTT 3.1.1 packet types ─────────────────────────────────
enum {
    MQTT_CONNECT     = 0x10,
    MQTT_CONNACK     = 0x20,
    MQTT_PUBLISH     = 0x30,
    MQTT_SUBACK      = 0x90,
    MQTT_SUBSCRIBE   = 0x82,
    MQTT_PINGREQ     = 0xC0,
    MQTT_PINGRESP    = 0xD0,
    MQTT_DISCONNECT  = 0xE0,
};

// ── Helpers ──────────────────────────────────────────────────

int MqttSocketAdapter::encode_remaining_length(uint8_t* buf, int len) {
    int pos = 0;
    do {
        uint8_t b = len % 128;
        len /= 128;
        if (len > 0) b |= 0x80;
        buf[pos++] = b;
    } while (len > 0);
    return pos;
}

int MqttSocketAdapter::write_u16(uint8_t* buf, uint16_t v) {
    buf[0] = (v >> 8) & 0xFF;
    buf[1] = v & 0xFF;
    return 2;
}

int MqttSocketAdapter::write_string(uint8_t* buf, const char* s, int len) {
    if (len < 0) len = (int)strlen(s);
    int pos = write_u16(buf, (uint16_t)len);
    memcpy(buf + pos, s, (size_t)len);
    return pos + len;
}

// ── Constructor / Destructor ─────────────────────────────────

MqttSocketAdapter::MqttSocketAdapter(IMqttMessageSink& sink, ITimeSource& time)
    : sink_(sink), time_(time) { sub_count_ = 0; }
MqttSocketAdapter::~MqttSocketAdapter() { disconnect(); }

// ── connect ──────────────────────────────────────────────────

bool MqttSocketAdapter::connect(const char* uri, const char* user,
                                 const char* pass,
                                 const char* lwt_topic, const char* lwt_msg,
                                 bool /*clean*/, int keepalive_sec)
{
    snprintf(uri_, sizeof(uri_), "%s", uri);
    keepalive_s_ = keepalive_sec;
    snprintf(lwt_topic_, sizeof(lwt_topic_), "%s", lwt_topic ? lwt_topic : "");
    snprintf(lwt_msg_,   sizeof(lwt_msg_),   "%s", lwt_msg   ? lwt_msg   : "");
    snprintf(saved_user_, sizeof(saved_user_), "%s", user ? user : "");
    snprintf(saved_pass_, sizeof(saved_pass_), "%s", pass ? pass : "");
    connect_pending_ = true;
    last_connect_attempt_us_ = 0;  // immediate reconnect on user action
    connect_failures_ = 0;
    return true;
}

bool MqttSocketAdapter::try_connect()
{
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }

    // Parse mqtt://host:port
    const char* host = uri_;
    int port = 1883;
    const char* colon = strstr(uri_, "://");
    if (colon) host = colon + 3;
    const char* port_str = strrchr(host, ':');
    if (port_str) {
        port = atoi(port_str + 1);
    }

    // Extract hostname (remove port suffix if present)
    char host_only[128];
    {
        const char* h = host;
        const char* p = strrchr(h, ':');
        int l = p ? (int)(p - h) : (int)strlen(h);
        if (l >= (int)sizeof(host_only)) l = (int)sizeof(host_only) - 1;
        memcpy(host_only, h, (size_t)l);
        host_only[l] = '\0';
    }

    ESP_LOGI(TAG, "Connecting to %s:%d...", host_only, port);

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

    // Disable Nagle — CONNECT is small, send immediately
    int nodelay = 1;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    // SO_LINGER with timeout=0 → close() sends RST instead of FIN
    // → no TIME_WAIT, socket freed immediately
    struct linger ling = {1, 0};
    setsockopt(sock_, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    // Try to parse as IP, otherwise need DNS resolution
    if (inet_pton(AF_INET, host_only, &addr.sin_addr) != 1) {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        if (getaddrinfo(host_only, nullptr, &hints, &res) != 0 || !res) {
            ESP_LOGE(TAG, "DNS resolve failed for %s", host_only);
            force_close_socket(sock_); return false;
        }
        memcpy(&addr, res->ai_addr, sizeof(addr));
        freeaddrinfo(res);
    }

    int ret = ::connect(sock_, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        ESP_LOGE(TAG, "connect() failed: %d", errno);
        force_close_socket(sock_);
        return false;
    }

    // Wait for TCP connection to be established (async connect via select)
    if (ret < 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock_, &wfds);
        struct timeval tv = {5, 0};  // 5-second timeout
        int sel = select(sock_ + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0) {
            ESP_LOGE(TAG, "TCP connect timeout");
            force_close_socket(sock_);
            return false;
        }
    }

    if (!send_connect_packet()) {
        ESP_LOGE(TAG, "CONNECT send failed: errno=%d", errno);
        force_close_socket(sock_);
        return false;
    }
    ESP_LOGI(TAG, "CONNECT sent, waiting for CONNACK...");
    return true;
}

bool MqttSocketAdapter::send_connect_packet()
{
    uint8_t buf[BUF_SIZE];
    int pos = 0;

    buf[pos++] = MQTT_CONNECT;  // type + flags

    // Build payload first to know remaining length
    uint8_t payload[BUF_SIZE];
    int pp = 0;

    // Client ID
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP32_%06X",
             (unsigned)(esp_random() & 0xFFFFFF));
    pp += write_string(payload + pp, client_id, -1);

    // Will topic + message (if present)
    if (lwt_topic_[0]) {
        pp += write_string(payload + pp, lwt_topic_, -1);
        pp += write_string(payload + pp, lwt_msg_, -1);
    }
    // Username + password (if present)
    if (saved_user_[0]) pp += write_string(payload + pp, saved_user_, -1);
    if (saved_pass_[0]) pp += write_string(payload + pp, saved_pass_, -1);

    // Variable header
    uint8_t vh[10];
    int vh_pos = 0;
    vh_pos += write_string(vh + vh_pos, "MQTT", 4);  // Protocol name
    vh[vh_pos++] = 4;                                  // Protocol level
    uint8_t flags = 0x02;  // CleanSession
    if (lwt_topic_[0]) flags |= 0x04   // Will flag
                             | 0x08    // Will QoS 1
                             | 0x20;   // Will Retain
    if (saved_user_[0]) flags |= 0x80; // User Name flag
    if (saved_pass_[0]) flags |= 0x40; // Password flag
    vh[vh_pos++] = flags;
    vh_pos += write_u16(vh + vh_pos, (uint16_t)keepalive_s_);

    int remaining = vh_pos + pp;
    pos += encode_remaining_length(buf + pos, remaining);
    memcpy(buf + pos, vh, (size_t)vh_pos); pos += vh_pos;
    memcpy(buf + pos, payload, (size_t)pp); pos += pp;

    // Debug: print hex dump
    char hex[256] = {};
    for (int i = 0; i < pos && i < 120; i++) {
        snprintf(hex + i*2, 3, "%02X", buf[i]);
    }
    ESP_LOGI(TAG, "CONNECT packet (%d bytes): %s", pos, hex);

    // Send with EAGAIN handling (non-blocking socket)
    int total_sent = 0;
    while (total_sent < pos) {
        int sent = send(sock_, buf + total_sent, pos - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "send() failed: errno=%d", errno);
            return false;
        }
        total_sent += sent;
    }
    return true;
}

// ── publish ──────────────────────────────────────────────────

int MqttSocketAdapter::publish(const char* topic, const char* data,
                                int len, QoS /*qos*/, bool retain)
{
    if (sock_ < 0) return -1;
    // Always send as QoS 0 — no outbox, no retransmit, no alloc
    return send_publish_packet(topic, data, len, retain) ? 1 : -1;
}

bool MqttSocketAdapter::send_publish_packet(const char* topic,
                                             const char* data,
                                             int data_len, bool retain)
{
    uint8_t buf[BUF_SIZE];
    int pos = 0;

    uint8_t type = MQTT_PUBLISH;
    if (retain) type |= 0x01;  // retain flag
    buf[pos++] = type;

    int topic_len = (int)strlen(topic);
    int actual_data_len = (data_len >= 0) ? data_len : (int)strlen(data);
    int remaining = 2 + topic_len + actual_data_len;  // 2 for topic length prefix
    pos += encode_remaining_length(buf + pos, remaining);
    pos += write_string(buf + pos, topic, topic_len);
    memcpy(buf + pos, data, (size_t)actual_data_len);
    pos += actual_data_len;

    // EAGAIN retry (non-blocking socket)
    int total_sent = 0;
    while (total_sent < pos) {
        int sent = send(sock_, buf + total_sent, pos - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            return false;
        }
        total_sent += sent;
    }
    return true;
}

// ── subscribe ────────────────────────────────────────────────

int MqttSocketAdapter::subscribe(const char* topic, QoS /*qos*/)
{
    if (sub_count_ >= MAX_SUBS) return -1;
    snprintf(sub_topics_[sub_count_], sizeof(sub_topics_[sub_count_]), "%s", topic);
    sub_count_++;

    if (!connected_) return 0;

    send_subscribe_packet();
    return 1;
}

bool MqttSocketAdapter::send_subscribe_packet()
{
    if (sub_count_ == 0) return true;

    uint8_t buf[BUF_SIZE];
    int pos = 0;
    buf[pos++] = MQTT_SUBSCRIBE;  // type + flags

    int remaining = 2;  // packet ID
    for (int i = 0; i < sub_count_; i++)
        remaining += 2 + (int)strlen(sub_topics_[i]) + 1;  // topic + QoS byte

    pos += encode_remaining_length(buf + pos, remaining);
    pos += write_u16(buf + pos, 1);  // packet ID = 1

    for (int i = 0; i < sub_count_; i++) {
        pos += write_string(buf + pos, sub_topics_[i], -1);
        buf[pos++] = 0;  // QoS 0
    }

    return send(sock_, buf, pos, 0) == pos;
}

// ── ping ─────────────────────────────────────────────────────

bool MqttSocketAdapter::send_pingreq()
{
    uint8_t buf[2] = { MQTT_PINGREQ, 0x00 };
    int sent = send(sock_, buf, 2, 0);
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // Non-blocking send — try again with short wait
        vTaskDelay(pdMS_TO_TICKS(50));
        sent = send(sock_, buf, 2, 0);
    }
    return sent == 2;
}

// ── disconnect ───────────────────────────────────────────────

static void force_close_socket(int& sock)
{
    if (sock < 0) return;
    // SO_LINGER with timeout=0 forces RST on close — frees lwIP buffers
    // immediately. Critical when WiFi is down and TCP FIN can't reach peer.
    struct linger l = {1, 0};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    close(sock);
    sock = -1;
}

void MqttSocketAdapter::disconnect()
{
    if (sock_ >= 0) {
        // Send MQTT DISCONNECT packet (0xE0 0x00) before closing
        // Tells broker we're leaving cleanly — prevents ghost sessions
        uint8_t disc[2] = {0xE0, 0x00};
        send(sock_, disc, 2, 0);
        force_close_socket(sock_);
    }
    connected_ = false;
    subscribed_ = false;
    ping_pending_ = false;
}

bool MqttSocketAdapter::reconnect() {
    connect_pending_ = true;
    return true;
}

bool MqttSocketAdapter::is_connected() const { return connected_; }

void MqttSocketAdapter::set_event_callback(EventCallback cb, void* ctx) {
    user_cb_ = cb; user_ctx_ = ctx;
}

int MqttSocketAdapter::unsubscribe(const char*) { return -1; }

// ── socket I/O ───────────────────────────────────────────────

int MqttSocketAdapter::read_byte(int timeout_ms) {
    uint8_t b;
    return read_exact(&b, 1, timeout_ms) == 1 ? b : -1;
}

int MqttSocketAdapter::read_exact(uint8_t* buf, int len, int timeout_ms) {
    int total = 0;
    uint64_t deadline = time_.monotonic_us() + (uint64_t)timeout_ms * 1000ULL;
    while (total < len) {
        int64_t remaining_us = (int64_t)(deadline - time_.monotonic_us());
        if (remaining_us <= 0) return total;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        struct timeval tv = { 0, (int)((remaining_us > 1000000) ? 1000000 : remaining_us) };
        int r = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
        if (r < 0) return total;
        if (r == 0) continue;

        int n = recv(sock_, buf + total, len - total, 0);
        if (n <= 0) return total;
        total += n;
    }
    return total;
}

void MqttSocketAdapter::process_incoming()
{
    static uint8_t buf[BUF_SIZE];  // static — не на стеке main_poller'а
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        struct timeval tv = {0, 0};
        int sel = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
        if (sel < 0) break;  // transient select error, retry next cycle
        if (sel == 0) break;  // no data

        int total = 0, to_read = 2;
        while (total < to_read && total < BUF_SIZE) {
            FD_ZERO(&fds); FD_SET(sock_, &fds);
            struct timeval t = {0, 50000};  // 50ms per chunk — не блокирует main_poller
            if (select(sock_ + 1, &fds, nullptr, nullptr, &t) <= 0) break;
            int n = recv(sock_, buf + total, BUF_SIZE - total, 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (n == 0) {
                // Peer closed connection
                ESP_LOGW(TAG, "recv()=0 — брокер закрыл соединение");
                connected_ = false; force_close_socket(sock_);
                if (user_cb_) user_cb_(2, (void*)"брокер закрыл", user_ctx_);
                return;
            }
            if (n < 0) break;  // transient error, retry next cycle
            total += n;
            if (total >= 2 && to_read == 2) {
                int rl = 0, m = 1, p = 1;
                while (p < total) { rl += (buf[p] & 0x7F) * m; m *= 128; if (!(buf[p++] & 0x80)) break; }
                to_read = p + rl;
            }
        }
        if (total < 2) break;  // partial or empty — retry next cycle

        last_recv_us_ = time_.monotonic_us();  // watchdog reset on successful read

        int type = buf[0];
        int p = 1; while (p < total && (buf[p] & 0x80)) p++; p++;
        int plen = total - p;
        uint8_t* payload = buf + p;

        switch (type & 0xF0) {
        case 0x20:
            if (plen >= 2 && payload[1] == 0) {
                connected_ = true; last_ping_us_ = time_.monotonic_us();
                last_recv_us_ = last_ping_us_;
                ping_pending_ = false; connect_failures_ = 0;
                ESP_LOGI(TAG, "CONNACK accepted");
                if (sub_count_ > 0) send_subscribe_packet();
                if (user_cb_) user_cb_(1, nullptr, user_ctx_);
            }
            break;
        case 0xD0: ping_pending_ = false; break;
        case 0x30:
            if (plen >= 2) {
                int tl = (payload[0] << 8) | payload[1];
                if (plen >= 2 + tl) {
                    IMqttMessageSink::Message msg;
                    int tlc = tl < (int)sizeof(msg.topic)-1 ? tl : (int)sizeof(msg.topic)-1;
                    memcpy(msg.topic, payload+2, tlc); msg.topic[tlc] = '\0';
                    int plc = (plen-2-tl) < (int)sizeof(msg.payload)-1 ? (plen-2-tl) : (int)sizeof(msg.payload)-1;
                    memcpy(msg.payload, payload+2+tl, plc); msg.payload[plc] = '\0';
                    msg.payload_len = plc; sink_.push(msg);
                }
            }
            break;
        case 0x90: if (plen >= 1) subscribed_ = true; break;
        }
    }
}

// ── poll_socket ──────────────────────────────────────────────

void MqttSocketAdapter::poll_socket()
{
    uint64_t now = time_.monotonic_us();

    // Try to connect if pending (with aggressive backoff —
    // each socket()/close() cycle leaks ~1.3KB in lwip)
    if (connect_pending_ && !connected_) {
        int backoff_s;
        if (connect_failures_ < 3)       backoff_s = 10;
        else if (connect_failures_ < 6)   backoff_s = 60;
        else                              backoff_s = 600;  // 10 min — don't fragment heap
        if (now - last_connect_attempt_us_ > (uint64_t)backoff_s * 1000000ULL) {
            last_connect_attempt_us_ = now;
            if (try_connect()) {
                connect_pending_ = false;
                connect_failures_ = 0;
            } else {
                if (connect_failures_ == 0 && user_cb_)
                    user_cb_(2, (void*)"ошибка подключения", user_ctx_);
                connect_failures_++;
            }
        }
    }

    if (sock_ < 0) return;

    // Read incoming data (CONNACK, PINGRESP, PUBLISH, SUBACK)
    process_incoming();

    // Refresh timestamp — process_incoming() may have updated last_recv_us_/last_ping_us_
    now = time_.monotonic_us();

    // Keepalive — send PINGREQ, only set pending if actually sent
    if (connected_ && now - last_ping_us_ > (uint64_t)(keepalive_s_ - 5) * 1000000ULL) {
        if (send_pingreq()) {
            ping_sent_us_ = now;
            ping_pending_ = true;
            last_ping_us_ = now;
        }
    }

    // Timeout — 60s grace period for sleeping laptop / spotty network
    if (ping_pending_ && now - ping_sent_us_ > 60000000ULL) {
        ESP_LOGW(TAG, "PINGRESP timeout — переподключение");
        disconnect();
        connect_pending_ = true;
        if (user_cb_) user_cb_(2, (void*)"PING таймаут", user_ctx_);
    }

    // Reconnect if socket died but connect not pending
    if (!connected_ && sock_ < 0 && !connect_pending_) {
        ESP_LOGW(TAG, "Соединение потеряно — переподключение");
        connect_pending_ = true;
        if (user_cb_) user_cb_(2, (void*)"переподключение", user_ctx_);
    }

    // Watchdog: if no data received for 90s, force reconnect.
    // Catches silent socket death where TCP RST isn't propagated.
    if (connected_ && last_recv_us_ > 0
        && now - last_recv_us_ > 90000000ULL) {
        ESP_LOGW(TAG, "Нет данных от брокера 90с — переподключение");
        disconnect();
        connect_pending_ = true;
        if (user_cb_) user_cb_(2, (void*)"нет данных 90с", user_ctx_);
    }
}
