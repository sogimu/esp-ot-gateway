#pragma once

#include <stdbool.h>

#define SENSOR_T1_GPIO  15
#define SENSOR_T2_GPIO  26

void sensors_init(void);
void sensors_poll(void);

extern float sensor1_temp;
extern float sensor2_temp;