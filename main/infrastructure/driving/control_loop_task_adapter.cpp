#include "infrastructure/driving/control_loop_task_adapter.h"
#include "application/ports/driving/icontrol_loop.h"

ControlLoopTaskAdapter::ControlLoopTaskAdapter(IControlLoop& poller)
    : poller_(poller)
{
}

void ControlLoopTaskAdapter::start()
{
    xTaskCreatePinnedToCore(task_loop, "main_poll", STACK_SIZE, this, PRIORITY, &task_, 1);
}

void ControlLoopTaskAdapter::stop()
{
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void ControlLoopTaskAdapter::task_loop(void* arg)
{
    auto* self = static_cast<ControlLoopTaskAdapter*>(arg);
    while (true) {
        self->poller_.run_once();
        vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
    }
}
