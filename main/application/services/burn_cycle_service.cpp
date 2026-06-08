#include "application/services/burn_cycle_service.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/itime_source.h"
#include <cstring>
#include <cstdlib>

BurnCycleService::BurnCycleService(IHeatingStateStore& state, ITimeSource& time)
    : state_(state), time_(time)
{
    burn_dur_ = static_cast<uint16_t*>(malloc(RING * sizeof(uint16_t)));
    pause_dur_ = static_cast<uint16_t*>(malloc(RING * sizeof(uint16_t)));
    if (burn_dur_) std::memset(burn_dur_, 0, RING * sizeof(uint16_t));
    if (pause_dur_) std::memset(pause_dur_, 0, RING * sizeof(uint16_t));
}

BurnCycleService::~BurnCycleService()
{
    free(burn_dur_);
    free(pause_dur_);
}

void BurnCycleService::poll()
{
    uint32_t now_ms = static_cast<uint32_t>(time_.now_us() / 1000);

    state_.lock_shared();
    bool flame = state_.is_flame_on();
    state_.unlock_shared();

    if (flame != prev_flame_) {
        if (flame) {
            // Flame ON: record preceding pause, then advance ring index
            flame_on_ms_ = now_ms;
            if (flame_off_ms_ > 0) {
                uint32_t pause = (now_ms - flame_off_ms_) / 1000;
                if (pause < 65535) {
                    pause_dur_[cycle_idx_] = static_cast<uint16_t>(pause);
                }
                cycle_idx_ = (cycle_idx_ + 1) % RING;
                if (cycle_total_ < RING) cycle_total_++;
            }
        } else {
            // Flame OFF: record burn at current position (same slot as upcoming pause)
            flame_off_ms_ = now_ms;
            if (flame_on_ms_ > 0) {
                uint32_t burn = (now_ms - flame_on_ms_) / 1000;
                if (burn < 65535) {
                    burn_dur_[cycle_idx_] = static_cast<uint16_t>(burn);
                }
                burner_sec_ += burn;
                cycle_cnt_++;
            }
        }
        prev_flame_ = flame;
    }

    last_tick_ms_ = now_ms;
}

void BurnCycleService::reset()
{
    std::memset(burn_dur_, 0, RING * sizeof(uint16_t));
    std::memset(pause_dur_, 0, RING * sizeof(uint16_t));
    cycle_idx_ = 0;
    cycle_total_ = 0;
    burner_sec_ = 0;
    cycle_cnt_ = 0;
}

float BurnCycleService::median(uint16_t* arr, int count)
{
    if (count == 0) return 0;
    // Copy and sort
    uint16_t sorted[RING];
    for (int i = 0; i < count; i++) sorted[i] = arr[i];
    std::sort(sorted, sorted + count);
    if (count % 2 == 0)
        return (sorted[count/2 - 1] + sorted[count/2]) / 2.0f;
    return static_cast<float>(sorted[count/2]);
}

float BurnCycleService::average(uint16_t* arr, int count)
{
    if (count == 0) return 0;
    uint32_t sum = 0;
    for (int i = 0; i < count; i++) sum += arr[i];
    return static_cast<float>(sum) / count;
}

float BurnCycleService::median_burn()  const { return median(const_cast<uint16_t*>(burn_dur_), cycle_cnt_); }
float BurnCycleService::median_pause() const { return median(const_cast<uint16_t*>(pause_dur_), cycle_total_); }
float BurnCycleService::avg_burn()     const { return average(const_cast<uint16_t*>(burn_dur_), cycle_cnt_); }
float BurnCycleService::avg_pause()    const { return average(const_cast<uint16_t*>(pause_dur_), cycle_total_); }
