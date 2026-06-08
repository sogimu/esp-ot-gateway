#include "infrastructure/freertos/shared_mutex.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void shared_mutex_init(SharedMutex* m)
{
    m->mutex = xSemaphoreCreateBinary();
    xSemaphoreGive(m->mutex);
    m->readers = 0;
    portMUX_INITIALIZE(&m->count_mux);
}

void shared_mutex_lock_shared(SharedMutex* m)
{
    portENTER_CRITICAL(&m->count_mux);
    m->readers++;
    if (m->readers == 1) {
        // First reader: acquire the resource semaphore (blocks writers)
        portEXIT_CRITICAL(&m->count_mux);
        xSemaphoreTake(m->mutex, portMAX_DELAY);
    } else {
        portEXIT_CRITICAL(&m->count_mux);
    }
}

void shared_mutex_unlock_shared(SharedMutex* m)
{
    portENTER_CRITICAL(&m->count_mux);
    m->readers--;
    if (m->readers == 0) {
        // Last reader: release the resource semaphore
        portEXIT_CRITICAL(&m->count_mux);
        xSemaphoreGive(m->mutex);
    } else {
        portEXIT_CRITICAL(&m->count_mux);
    }
}

void shared_mutex_lock_exclusive(SharedMutex* m)
{
    // Acquire the resource semaphore — blocks if readers/writer hold it
    xSemaphoreTake(m->mutex, portMAX_DELAY);
}

void shared_mutex_unlock_exclusive(SharedMutex* m)
{
    xSemaphoreGive(m->mutex);
}
