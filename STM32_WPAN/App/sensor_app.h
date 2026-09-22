#ifndef SENSOR_APP_H
#define SENSOR_APP_H

#include <stdint.h>

void Sensor_App_Init(void);
void Sensor_App_SetNotificationEnabled(uint8_t enabled);
void Sensor_App_ResumeNotification(void);
void Sensor_App_SetLed(uint8_t enabled);

#endif
