#include "app_bme280.h"

#include "app_state.h"
#include "bme280_sensor.h"
#include "debug_log.h"

#define BME280_THREAD_STACK_SIZE          2048U
#define BME280_THREAD_PRIORITY              18U
#define BME280_SAMPLE_PERIOD_SECONDS          1U
#define BME280_RETRY_PERIOD_SECONDS           5U
#define BME280_LOG_PERIOD_SECONDS            30U
#define BME280_FAILURES_BEFORE_REINIT          3U

static TX_THREAD bme280_thread;
static UCHAR *bme280_thread_stack;

static VOID BME280_Thread_Entry(ULONG argument);

static ULONG seconds_to_ticks(ULONG seconds)
{
  ULONG ticks = seconds * TX_TIMER_TICKS_PER_SECOND;
  return (ticks != 0U) ? ticks : 1U;
}

static void log_detection_failure(BME280_SENSOR_STATUS status)
{
  if (status == BME280_SENSOR_NOT_FOUND)
  {
    Debug_Log_Line("[BME280] not detected at 0x76 or 0x77; retrying");
  }
  else if (status == BME280_SENSOR_WRONG_CHIP_ID)
  {
    Debug_Log_Hex("[BME280] responding 7-bit address: ",
                  BME280_Sensor_GetAddress());
    Debug_Log_Hex("[BME280] unsupported chip ID: ",
                  BME280_Sensor_GetChipId());
    Debug_Log_Hex("[BME280] expected chip ID:    ",
                  BME280_SENSOR_CHIP_ID);
  }
  else
  {
    Debug_Log_U32("[BME280] initialization error: ", (uint32_t)status);
  }
}

static void log_sample(const BME280_SENSOR_SAMPLE *sample)
{
  Debug_Log_I32("[BME280] temperature x100 C: ", sample->temperature);
  Debug_Log_U32("[BME280] pressure x100 Pa:   ", sample->pressure);
  Debug_Log_U32("[BME280] humidity x1024 %:  ", sample->humidity);
}

UINT App_BME280_Init(TX_BYTE_POOL *byte_pool)
{
  UINT status;

  if (byte_pool == TX_NULL)
  {
    return TX_PTR_ERROR;
  }

  status = tx_byte_allocate(byte_pool, (VOID **)&bme280_thread_stack,
                            BME280_THREAD_STACK_SIZE, TX_NO_WAIT);
  if (status != TX_SUCCESS)
  {
    Debug_Log_U32("[BME280] thread stack allocation failed: ", status);
    return status;
  }

  status = tx_thread_create(&bme280_thread, "BME280 sensor",
                            BME280_Thread_Entry, 0U,
                            bme280_thread_stack, BME280_THREAD_STACK_SIZE,
                            BME280_THREAD_PRIORITY, BME280_THREAD_PRIORITY,
                            TX_NO_TIME_SLICE, TX_AUTO_START);
  if (status != TX_SUCCESS)
  {
    Debug_Log_U32("[BME280] thread creation failed: ", status);
    (void)tx_byte_release(bme280_thread_stack);
    bme280_thread_stack = TX_NULL;
    return status;
  }

  Debug_Log_Line("[BME280] sensor thread created");
  return TX_SUCCESS;
}

static VOID BME280_Thread_Entry(ULONG argument)
{
  BME280_SENSOR_SAMPLE sample;
  BME280_SENSOR_STATUS status;
  ULONG last_log_time = 0U;
  ULONG now;
  uint32_t consecutive_failures;
  uint8_t detection_error_logged = 0U;

  (void)argument;
  App_State_SetSensor(0, 0U, 0U);
  Debug_Log_Line("[BME280] sensor thread started on I2C1 PB8/PB9");

  for (;;)
  {
    status = BME280_Sensor_Init();
    if (status != BME280_SENSOR_OK)
    {
      if (detection_error_logged == 0U)
      {
        log_detection_failure(status);
        detection_error_logged = 1U;
      }
      App_State_SetSensor(0, 0U, 0U);
      tx_thread_sleep(seconds_to_ticks(BME280_RETRY_PERIOD_SECONDS));
      continue;
    }

    detection_error_logged = 0U;
    consecutive_failures = 0U;
    last_log_time = 0U;
    Debug_Log_Line("[BME280] detected and initialized");
    Debug_Log_Hex("[BME280] 7-bit I2C address: ",
                  BME280_Sensor_GetAddress());
    Debug_Log_Hex("[BME280] chip ID:          ",
                  BME280_Sensor_GetChipId());

    for (;;)
    {
      status = BME280_Sensor_Read(&sample);
      if (status == BME280_SENSOR_OK)
      {
        App_State_SetSensor(sample.temperature, sample.pressure,
                            sample.humidity);
        consecutive_failures = 0U;
        now = tx_time_get();
        if ((last_log_time == 0U) ||
            ((now - last_log_time) >=
             seconds_to_ticks(BME280_LOG_PERIOD_SECONDS)))
        {
          last_log_time = now;
          log_sample(&sample);
        }
      }
      else
      {
        consecutive_failures++;
        if (consecutive_failures == 1U)
        {
          Debug_Log_U32("[BME280] measurement error: ", (uint32_t)status);
        }
        if (consecutive_failures >= BME280_FAILURES_BEFORE_REINIT)
        {
          Debug_Log_Line("[BME280] connection lost; reinitializing");
          App_State_SetSensor(0, 0U, 0U);
          break;
        }
      }

      tx_thread_sleep(seconds_to_ticks(BME280_SAMPLE_PERIOD_SECONDS));
    }
  }
}
