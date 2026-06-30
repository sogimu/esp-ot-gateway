#include "infrastructure/driving/main_poller_task_adapter.h"
#include "application/ports/driving/imain_poller.h"

MainPollerTaskAdapter::MainPollerTaskAdapter(IMainPoller& poller)
    : poller_(poller)
{
}

void MainPollerTaskAdapter::start()
{
    xTaskCreatePinnedToCore(task_loop, "main_poll", STACK_SIZE, this, PRIORITY, &task_, 1);
}

void MainPollerTaskAdapter::stop()
{
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void MainPollerTaskAdapter::task_loop(void* arg)
{
    auto* self = static_cast<MainPollerTaskAdapter*>(arg);
    while (true) {
        self->poller_.run_once();
        vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
    }
}
