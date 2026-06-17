#pragma once

#include "application/ports/driven/iwifi_credential_store.h"
#include <cstring>

/// Fake IWifiCredentialStore for host unit testing.
class FakeWifiCredentialStore : public IWifiCredentialStore {
public:
    bool has_data_ = false;
    int  mode_ = 0;
    char sta_ssid_[33] = {};
    char sta_pass_[65] = {};
    char ap_pass_[65] = {};
    uint8_t fail_cnt_ = 0;

    bool erase_called_ = false;
    int  save_called_ = 0;

    void reset() {
        has_data_ = false; mode_ = 0; fail_cnt_ = 0;
        erase_called_ = false; save_called_ = 0;
        memset(sta_ssid_, 0, 33); memset(sta_pass_, 0, 65);
        memset(ap_pass_, 0, 65);
    }

    bool load(int& mode, char* sta_ssid, size_t ssid_sz,
              char* sta_pass, size_t pass_sz,
              char* ap_pass, size_t ap_sz) override {
        if (!has_data_) return false;
        mode = mode_;
        if (sta_ssid) { strncpy(sta_ssid, sta_ssid_, ssid_sz); sta_ssid[ssid_sz - 1] = '\0'; }
        if (sta_pass) { strncpy(sta_pass, sta_pass_, pass_sz); sta_pass[pass_sz - 1] = '\0'; }
        if (ap_pass)  { strncpy(ap_pass,  ap_pass_,  ap_sz);  ap_pass[ap_sz - 1]   = '\0'; }
        return true;
    }

    void save(int mode, const char* sta_ssid, const char* sta_pass,
              const char* ap_pass) override {
        save_called_++;
        has_data_ = true;
        mode_ = mode;
        if (sta_ssid) strncpy(sta_ssid_, sta_ssid, 32);
        else sta_ssid_[0] = '\0';
        if (sta_pass) strncpy(sta_pass_, sta_pass, 63);
        else sta_pass_[0] = '\0';
        if (ap_pass)  strncpy(ap_pass_,  ap_pass,  63);
        else ap_pass_[0] = '\0';
    }

    void erase() override {
        erase_called_ = true;
        has_data_ = false; mode_ = 0; fail_cnt_ = 0;
        memset(sta_ssid_, 0, 33); memset(sta_pass_, 0, 65);
        memset(ap_pass_, 0, 65);
    }

    uint8_t sta_fail_count() override { return fail_cnt_; }
    void set_sta_fail_count(uint8_t c) override { fail_cnt_ = c; }
};
