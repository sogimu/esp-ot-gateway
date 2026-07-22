#pragma once
#include <cstdint>
struct OtaStatus {
  enum State { IDLE, FETCHING, WRITING, VERIFY_PENDING, DONE, ERROR } state;
  int  progress_pct;
  char last_error[96];
  char current_version[32];
  char target_tag[32];
  bool rollback_pending;
};
class IOtaManager {
public:
  virtual ~IOtaManager() = default;
  virtual OtaStatus status() = 0;
  virtual char* fetch_version_list() = 0;     // heap JSON, caller frees; nullptr при ошибке
  virtual bool begin_update(const char* tag) = 0;
  virtual void rollback_now() = 0;
  virtual void poll() = 0;
};
