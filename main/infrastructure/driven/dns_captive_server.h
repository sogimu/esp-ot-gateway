#pragma once

/// Minimal DNS server for captive portal detection.
/// Listens on UDP port 53, responds to ALL A-record queries with 192.168.4.1.
/// Runs ONLY during FIRST_BOOT provisioning. Call start()/stop() explicitly.
class DnsCaptiveServer {
public:
    void start();
    void stop();
    bool is_running() const { return running_; }
    bool start_called() const { return start_called_; }

private:
    bool running_ = false;
    bool start_called_ = false;
    int  sock_ = -1;
    // FreeRTOS task handle (created in start, deleted in stop)
    void* task_ = nullptr;

    static void dns_task(void* arg);
    static const char* TAG;
};
