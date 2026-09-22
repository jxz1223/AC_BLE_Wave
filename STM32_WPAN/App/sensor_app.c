#include "main.h"
#include "ble.h"
#include "custom_stm.h"
#include "sensor_app.h"
#include "stm32_seq.h"

#define ADC_CHANNEL_COUNT                 4U
#define ADC_CONVERTED_DATA_BUFFER_SIZE   76U
#define WAVE_FRAME_SIZE                  246U
#define WAVE_HEADER_SIZE                  12U
#define WAVE_SAMPLE_COUNT                76U
#define WAVE_RESERVED_OFFSET             240U
#define WAVE_CRC_OFFSET                 244U
#define WAVE_SAMPLE_RATE               2000U

#define ADC_NOMINAL_CENTER             2048U
#define AGC_MAX_CODE                     7U
#define AGC_WINDOW_FRAMES                13U
#define AGC_CONFIRM_WINDOWS              3U
#define AGC_HOLDOFF_FRAMES              53U
#define AGC_REDUCE_NEGATIVE_PEAK      1600U
#define AGC_REDUCE_POSITIVE_PEAK      1600U
#define AGC_RAISE_PEAK                 600U
#define NORMALIZED_CENTER ((uint32_t)ADC_NOMINAL_CENTER * (1UL << AGC_MAX_CODE))
#define NORMALIZED_MAX                 0x7FFFFUL

typedef enum
{
  SENSOR_LED_BLUE = 0,
  SENSOR_LED_GREEN,
  SENSOR_LED_RED
} Sensor_Led_t;

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

static uint8_t notification_enabled = 1U;
static uint8_t notify_buffer[WAVE_FRAME_SIZE];
static __IO uint16_t adc_buffer[ADC_CONVERTED_DATA_BUFFER_SIZE];
static volatile uint8_t frame_ready = 0U;
static uint8_t frame_fill_active = 0U;
static uint16_t frame_sequence = 0U;
static uint32_t scan_counter = 0U;
static uint16_t notification_led_counter = 0U;
static uint8_t adc_led_counter = 0U;

static uint8_t gain_code[ADC_CHANNEL_COUNT] = {0U, 0U, 0U, 0U};
/* Each channel may later be calibrated independently with no input applied. */
static uint16_t adc_center[ADC_CHANNEL_COUNT] = {
  ADC_NOMINAL_CENTER, ADC_NOMINAL_CENTER,
  ADC_NOMINAL_CENTER, ADC_NOMINAL_CENTER
};
static uint16_t agc_min[ADC_CHANNEL_COUNT];
static uint16_t agc_max[ADC_CHANNEL_COUNT];
static uint8_t agc_window_count[ADC_CHANNEL_COUNT];
static uint8_t agc_high_confirm[ADC_CHANNEL_COUNT];
static uint8_t agc_low_confirm[ADC_CHANNEL_COUNT];
static uint8_t agc_holdoff_count[ADC_CHANNEL_COUNT];

static void Sensor_App_Process(void);
static void Set_Gain(uint8_t channel, uint8_t code);
static void Reset_Agc_Stats(void);
static void Reset_Agc_Channel_Stats(uint8_t channel);
static void Observe_Agc_Block(const volatile uint16_t *samples);
static uint16_t Crc16(const uint8_t *data, uint16_t length);
static uint32_t Normalize_Adc(uint16_t raw_adc, uint8_t channel);
static void Pack_Sample(uint32_t sample_index);

static GPIO_TypeDef *const led_ports[] = {Blue_Led_GPIO_Port, Green_Led_GPIO_Port, Red_Led_GPIO_Port};
static const uint16_t led_pins[] = {Blue_Led_Pin, Green_Led_Pin, Red_Led_Pin};

static void Set_Led(Sensor_Led_t led, GPIO_PinState state)
{
  HAL_GPIO_WritePin(led_ports[led], led_pins[led], state);
}

void Sensor_App_SetLed(uint8_t enabled)
{
  Set_Led(SENSOR_LED_RED, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Sensor_App_SetNotificationEnabled(uint8_t enabled)
{
  notification_enabled = enabled ? 1U : 0U;
}

void Sensor_App_ResumeNotification(void)
{
  notification_enabled = 1U;
  UTIL_SEQ_SetTask(1U << CFG_TASK_DATA_TRANSFER_UPDATE_ID, CFG_SCH_PRIO_0);
}

void Sensor_App_Init(void)
{
  uint32_t i;

  for (i = 0U; i < ADC_CHANNEL_COUNT; i++)
  {
    Set_Gain((uint8_t)i, 0U);
  }
  Reset_Agc_Stats();
  for (i = 0U; i < WAVE_FRAME_SIZE; i++)
  {
    notify_buffer[i] = 0U;
  }
  for (i = 0U; i < ADC_CONVERTED_DATA_BUFFER_SIZE; i++)
  {
    adc_buffer[i] = 0x1000U;
  }

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer,
                        ADC_CONVERTED_DATA_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }
  UTIL_SEQ_RegTask(1U << CFG_TASK_DATA_TRANSFER_UPDATE_ID,
                   UTIL_SEQ_RFU, Sensor_App_Process);
}

static void Sensor_App_Process(void)
{
  tBleStatus status;

  if ((notification_enabled == 0U) || (frame_ready == 0U))
  {
    if (notification_enabled != 0U)
    {
      UTIL_SEQ_SetTask(1U << CFG_TASK_DATA_TRANSFER_UPDATE_ID, CFG_SCH_PRIO_0);
    }
    return;
  }

  status = Custom_STM_App_Update_Char(CUSTOM_STM_TX_CHAR, notify_buffer);
  if (status == BLE_STATUS_INSUFFICIENT_RESOURCES)
  {
    notification_enabled = 0U;
    return;
  }

  frame_ready = 0U;
  notification_led_counter++;
  if (notification_led_counter == 20U)
  {
    Set_Led(SENSOR_LED_BLUE, GPIO_PIN_SET);
  }
  else if (notification_led_counter == 40U)
  {
    Set_Led(SENSOR_LED_BLUE, GPIO_PIN_RESET);
    notification_led_counter = 0U;
  }
  UTIL_SEQ_SetTask(1U << CFG_TASK_DATA_TRANSFER_UPDATE_ID, CFG_SCH_PRIO_0);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint32_t i;

  if (hadc != &hadc1)
  {
    return;
  }
  frame_fill_active = (frame_ready == 0U);
  if (frame_fill_active != 0U)
  {
    for (i = 0U; i < ADC_CONVERTED_DATA_BUFFER_SIZE / 2U; i++)
    {
      Pack_Sample(i);
    }
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint32_t i;
  uint16_t crc;

  if (hadc != &hadc1)
  {
    return;
  }
  adc_led_counter++;
  if (adc_led_counter > 20U)
  {
    Set_Led(SENSOR_LED_GREEN, GPIO_PIN_SET);
  }
  if (adc_led_counter > 40U)
  {
    Set_Led(SENSOR_LED_GREEN, GPIO_PIN_RESET);
    adc_led_counter = 0U;
  }

  if ((frame_fill_active != 0U) && (frame_ready == 0U))
  {
    for (i = ADC_CONVERTED_DATA_BUFFER_SIZE / 2U;
         i < ADC_CONVERTED_DATA_BUFFER_SIZE; i++)
    {
      Pack_Sample(i);
    }

    notify_buffer[0] = 0xA6U;
    notify_buffer[1] = 0x6AU;
    notify_buffer[2] = 0x02U;
    notify_buffer[3] = WAVE_SAMPLE_COUNT;
    notify_buffer[4] = (uint8_t)(frame_sequence >> 8);
    notify_buffer[5] = (uint8_t)frame_sequence;
    notify_buffer[6] = (uint8_t)(WAVE_SAMPLE_RATE >> 8);
    notify_buffer[7] = (uint8_t)WAVE_SAMPLE_RATE;
    notify_buffer[8] = (uint8_t)(scan_counter >> 24);
    notify_buffer[9] = (uint8_t)(scan_counter >> 16);
    notify_buffer[10] = (uint8_t)(scan_counter >> 8);
    notify_buffer[11] = (uint8_t)scan_counter;
    for (i = WAVE_RESERVED_OFFSET; i < WAVE_CRC_OFFSET; i++)
    {
      notify_buffer[i] = 0U;
    }
    crc = Crc16(notify_buffer, WAVE_CRC_OFFSET);
    notify_buffer[WAVE_CRC_OFFSET] = (uint8_t)(crc >> 8);
    notify_buffer[WAVE_CRC_OFFSET + 1U] = (uint8_t)crc;
    frame_ready = 1U;
  }
  frame_fill_active = 0U;
  frame_sequence++;
  scan_counter += ADC_CONVERTED_DATA_BUFFER_SIZE / ADC_CHANNEL_COUNT;
  Observe_Agc_Block(adc_buffer);
}

static uint16_t Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  uint8_t bit;

  for (i = 0U; i < length; i++)
  {
    crc ^= (uint16_t)data[i] << 8;
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) :
                              (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static uint32_t Normalize_Adc(uint16_t raw_adc, uint8_t channel)
{
  int32_t delta;
  int32_t scale;
  int32_t normalized;

  delta = (int32_t)(raw_adc & 0x0FFFU) - (int32_t)adc_center[channel];
  scale = 1L << (AGC_MAX_CODE - gain_code[channel]);
  normalized = (int32_t)NORMALIZED_CENTER + delta * scale;

  if (normalized < 0)
  {
    return 0U;
  }
  if ((uint32_t)normalized > NORMALIZED_MAX)
  {
    return NORMALIZED_MAX;
  }
  return (uint32_t)normalized;
}

static void Pack_Sample(uint32_t sample_index)
{
  uint8_t channel = (uint8_t)(sample_index % ADC_CHANNEL_COUNT);
  uint32_t normalized = Normalize_Adc(adc_buffer[sample_index], channel);
  uint32_t sample = normalized |
                    ((uint32_t)channel << 19) |
                    ((uint32_t)gain_code[channel] << 21);
  uint32_t offset = WAVE_HEADER_SIZE + sample_index * 3U;

  notify_buffer[offset] = (uint8_t)(sample >> 16);
  notify_buffer[offset + 1U] = (uint8_t)(sample >> 8);
  notify_buffer[offset + 2U] = (uint8_t)sample;
}

static void Set_Gain(uint8_t channel, uint8_t code)
{
  GPIO_TypeDef *a0_port;
  GPIO_TypeDef *a1_port;
  GPIO_TypeDef *a2_port;
  uint16_t a0_pin;
  uint16_t a1_pin;
  uint16_t a2_pin;

  if ((channel >= ADC_CHANNEL_COUNT) || (code > AGC_MAX_CODE))
  {
    return;
  }
  switch (channel)
  {
    case 0U: a0_port=U4_A0_GPIO_Port; a0_pin=U4_A0_Pin; a1_port=U4_A1_GPIO_Port; a1_pin=U4_A1_Pin; a2_port=U4_A2_GPIO_Port; a2_pin=U4_A2_Pin; break;
    case 1U: a0_port=U5_A0_GPIO_Port; a0_pin=U5_A0_Pin; a1_port=U5_A1_GPIO_Port; a1_pin=U5_A1_Pin; a2_port=U5_A2_GPIO_Port; a2_pin=U5_A2_Pin; break;
    case 2U: a0_port=U6_A0_GPIO_Port; a0_pin=U6_A0_Pin; a1_port=U6_A1_GPIO_Port; a1_pin=U6_A1_Pin; a2_port=U6_A2_GPIO_Port; a2_pin=U6_A2_Pin; break;
    default: a0_port=U7_A0_GPIO_Port; a0_pin=U7_A0_Pin; a1_port=U7_A1_GPIO_Port; a1_pin=U7_A1_Pin; a2_port=U7_A2_GPIO_Port; a2_pin=U7_A2_Pin; break;
  }
  HAL_GPIO_WritePin(a0_port, a0_pin, (code & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(a1_port, a1_pin, (code & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(a2_port, a2_pin, (code & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  gain_code[channel] = code;
}

static void Reset_Agc_Stats(void)
{
  uint8_t channel;

  for (channel = 0U; channel < ADC_CHANNEL_COUNT; channel++)
  {
    Reset_Agc_Channel_Stats(channel);
    agc_high_confirm[channel] = 0U;
    agc_low_confirm[channel] = 0U;
    agc_holdoff_count[channel] = 0U;
  }
}

static void Reset_Agc_Channel_Stats(uint8_t channel)
{
  agc_min[channel] = 0x0FFFU;
  agc_max[channel] = 0U;
  agc_window_count[channel] = 0U;
}

static void Observe_Agc_Block(const volatile uint16_t *samples)
{
  uint32_t i;
  uint8_t channel;

  for (channel = 0U; channel < ADC_CHANNEL_COUNT; channel++)
  {
    if (agc_holdoff_count[channel] != 0U)
    {
      agc_holdoff_count[channel]--;
      if (agc_holdoff_count[channel] == 0U)
      {
        Reset_Agc_Channel_Stats(channel);
      }
    }
  }

  for (i = 0U; i < ADC_CONVERTED_DATA_BUFFER_SIZE; i++)
  {
    channel = (uint8_t)(i % ADC_CHANNEL_COUNT);
    if (agc_holdoff_count[channel] != 0U)
    {
      continue;
    }
    if (samples[i] < agc_min[channel]) agc_min[channel] = samples[i];
    if (samples[i] > agc_max[channel]) agc_max[channel] = samples[i];
  }

  for (channel = 0U; channel < ADC_CHANNEL_COUNT; channel++)
  {
    uint8_t high_limit_exceeded;
    uint8_t low_limit_exceeded;

    if (agc_holdoff_count[channel] != 0U)
    {
      continue;
    }

    agc_window_count[channel]++;
    if (agc_window_count[channel] < AGC_WINDOW_FRAMES)
    {
      continue;
    }

    {
    int32_t negative_peak = (int32_t)adc_center[channel] - (int32_t)agc_min[channel];
    int32_t positive_peak = (int32_t)agc_max[channel] - (int32_t)adc_center[channel];
      high_limit_exceeded = ((negative_peak > (int32_t)AGC_REDUCE_NEGATIVE_PEAK) ||
                             (positive_peak > (int32_t)AGC_REDUCE_POSITIVE_PEAK)) ? 1U : 0U;
      low_limit_exceeded = ((negative_peak < (int32_t)AGC_RAISE_PEAK) &&
                            (positive_peak < (int32_t)AGC_RAISE_PEAK)) ? 1U : 0U;
    }

    if (high_limit_exceeded != 0U)
    {
      agc_low_confirm[channel] = 0U;
      if (agc_high_confirm[channel] < AGC_CONFIRM_WINDOWS)
      {
        agc_high_confirm[channel]++;
      }
      if ((agc_high_confirm[channel] >= AGC_CONFIRM_WINDOWS) && (gain_code[channel] > 0U))
      {
        Set_Gain(channel, (uint8_t)(gain_code[channel] - 1U));
        agc_high_confirm[channel] = 0U;
        agc_holdoff_count[channel] = AGC_HOLDOFF_FRAMES;
      }
    }
    else if (low_limit_exceeded != 0U)
    {
      agc_high_confirm[channel] = 0U;
      if (agc_low_confirm[channel] < AGC_CONFIRM_WINDOWS)
      {
        agc_low_confirm[channel]++;
      }
      if ((agc_low_confirm[channel] >= AGC_CONFIRM_WINDOWS) && (gain_code[channel] < AGC_MAX_CODE))
      {
        Set_Gain(channel, (uint8_t)(gain_code[channel] + 1U));
        agc_low_confirm[channel] = 0U;
        agc_holdoff_count[channel] = AGC_HOLDOFF_FRAMES;
      }
    }
    else
    {
      agc_high_confirm[channel] = 0U;
      agc_low_confirm[channel] = 0U;
    }

    Reset_Agc_Channel_Stats(channel);
  }
}
