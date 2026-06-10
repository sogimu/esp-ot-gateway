#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/// Read-write lock backed by FreeRTOS binary semaphore.
///
/// Multiple readers can hold lock_shared() concurrently.
/// Single writer gets lock_exclusive() — blocks until all readers release.
///
/// Usage:
///   SharedMutex m;
///   m.lock_shared();    // reader
///   // ... read ...
///   m.unlock_shared();
///
///   m.lock_exclusive(); // writer
///   // ... write ...
///   m.unlock_exclusive();

typedef struct {
    SemaphoreHandle_t mutex;       // binary — protects the reader count
    int               readers;     // active shared readers
    portMUX_TYPE      count_mux;   // spinlock for readers counter
} SharedMutex;

/// Initialize the mutex. Must be called before use.
void shared_mutex_init(SharedMutex* m);

/// Acquire shared (read) lock.
void shared_mutex_lock_shared(SharedMutex* m);

/// Release shared (read) lock.
void shared_mutex_unlock_shared(SharedMutex* m);

/// Acquire exclusive (write) lock — blocks until all readers release.
void shared_mutex_lock_exclusive(SharedMutex* m);

/// Release exclusive (write) lock.
void shared_mutex_unlock_exclusive(SharedMutex* m);

#ifdef __cplusplus
}
#endif
