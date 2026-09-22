#include "main.h"
#include "app_common.h"
#include "ble.h"
#include "custom_app.h"
#include "custom_stm.h"
#include "sensor_app.h"

void Custom_STM_App_Notification(Custom_STM_App_Notification_evt_t *event)
{
  switch (event->Custom_Evt_Opcode)
  {
    case CUSTOM_STM_TX_CHAR_NOTIFY_ENABLED_EVT:
      Sensor_App_SetNotificationEnabled(1U);
      Sensor_App_ResumeNotification();
      break;

    case CUSTOM_STM_TX_CHAR_NOTIFY_DISABLED_EVT:
      Sensor_App_SetNotificationEnabled(0U);
      break;

    case CUSTOM_STM_LED_C_WRITE_NO_RESP_EVT:
      if ((event->DataTransfered.pPayload != NULL) &&
          (event->DataTransfered.Length >= 2U))
      {
        Sensor_App_SetLed(event->DataTransfered.pPayload[1] != 0U);
      }
      break;

    case CUSTOM_STM_LED_C_READ_EVT:
    case CUSTOM_STM_NOTIFICATION_COMPLETE_EVT:
    case CUSTOM_STM_BOOT_REQUEST_EVT:
    default:
      break;
  }
}

void Custom_APP_Init(void)
{
  Sensor_App_Init();
}

void Custom_APP_Notification(Custom_App_ConnHandle_Not_evt_t *event)
{
  (void)event;
}

void Resume_Notification(void)
{
  Sensor_App_ResumeNotification();
}
