#include "infrastructure/driven/temperature_source_adapter.h"
#include "application/ports/driven/iheating_state_store.h"

float TemperatureSourceAdapter::t1()
{
    store_.lock_shared();
    float v = store_.get_t1_temp() + store_.get_t1_offset();
    store_.unlock_shared();
    return v;
}

float TemperatureSourceAdapter::t2()
{
    store_.lock_shared();
    float v = store_.get_t2_temp() + store_.get_t2_offset();
    store_.unlock_shared();
    return v;
}

float TemperatureSourceAdapter::outside()
{
    store_.lock_shared();
    float v = store_.get_outside_temp() + store_.get_outside_offset();
    store_.unlock_shared();
    return v;
}

float TemperatureSourceAdapter::ch_flow()
{
    store_.lock_shared();
    float v = store_.get_ch_temp();
    store_.unlock_shared();
    return v;
}

float TemperatureSourceAdapter::ch_return()
{
    store_.lock_shared();
    float v = store_.get_return_temp();
    store_.unlock_shared();
    return v;
}

float TemperatureSourceAdapter::dhw()
{
    store_.lock_shared();
    float v = store_.get_dhw_temp();
    store_.unlock_shared();
    return v;
}
