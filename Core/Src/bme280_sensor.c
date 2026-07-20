#include "bme280_sensor.h"

#include "i2c.h"
#include "main.h"

#include <stddef.h>

#define BME280_REG_CALIB_TP              0x88U
#define BME280_REG_CHIP_ID               0xD0U
#define BME280_REG_RESET                 0xE0U
#define BME280_REG_CALIB_H               0xE1U
#define BME280_REG_CTRL_HUM              0xF2U
#define BME280_REG_STATUS                0xF3U
#define BME280_REG_CTRL_MEAS             0xF4U
#define BME280_REG_CONFIG                0xF5U
#define BME280_REG_PRESS_MSB             0xF7U

#define BME280_RESET_COMMAND             0xB6U
#define BME280_STATUS_IM_UPDATE          0x01U
#define BME280_STATUS_MEASURING          0x08U

#define BME280_CTRL_HUM_X1               0x01U
#define BME280_CONFIG_FILTER_X16         0x10U
#define BME280_CTRL_MEAS_SLEEP           0x54U /* T x2, P x16, sleep. */
#define BME280_CTRL_MEAS_FORCED          0x55U /* T x2, P x16, forced. */

#define BME280_I2C_TIMEOUT_MS            50U
#define BME280_RESET_TIMEOUT_MS          50U
#define BME280_MEASUREMENT_TIMEOUT_MS   100U

typedef struct
{
  uint16_t dig_t1;
  int16_t dig_t2;
  int16_t dig_t3;
  uint16_t dig_p1;
  int16_t dig_p2;
  int16_t dig_p3;
  int16_t dig_p4;
  int16_t dig_p5;
  int16_t dig_p6;
  int16_t dig_p7;
  int16_t dig_p8;
  int16_t dig_p9;
  uint8_t dig_h1;
  int16_t dig_h2;
  uint8_t dig_h3;
  int16_t dig_h4;
  int16_t dig_h5;
  int8_t dig_h6;
  int32_t t_fine;
} BME280_CALIBRATION;

static BME280_CALIBRATION calibration;
static uint8_t sensor_address;
static uint8_t sensor_chip_id;
static uint8_t sensor_initialized;

static uint16_t get_u16_le(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static int16_t get_s16_le(const uint8_t *data)
{
  return (int16_t)get_u16_le(data);
}

static int16_t sign_extend_12(uint16_t value)
{
  if ((value & 0x0800U) != 0U)
  {
    value |= 0xF000U;
  }
  return (int16_t)value;
}

static HAL_StatusTypeDef read_registers(uint8_t address, uint8_t reg,
                                        uint8_t *data, uint16_t length)
{
  return HAL_I2C_Mem_Read(&hi2c1, (uint16_t)address << 1,
                          reg, I2C_MEMADD_SIZE_8BIT,
                          data, length, BME280_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef write_register(uint8_t address, uint8_t reg,
                                        uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1, (uint16_t)address << 1,
                           reg, I2C_MEMADD_SIZE_8BIT,
                           &value, 1U, BME280_I2C_TIMEOUT_MS);
}

static BME280_SENSOR_STATUS wait_status_clear(uint8_t mask, uint32_t timeout_ms,
                                               BME280_SENSOR_STATUS timeout_status)
{
  uint32_t started = HAL_GetTick();
  uint8_t status;

  do
  {
    if (read_registers(sensor_address, BME280_REG_STATUS, &status, 1U) != HAL_OK)
    {
      return BME280_SENSOR_BUS_ERROR;
    }
    if ((status & mask) == 0U)
    {
      return BME280_SENSOR_OK;
    }
    HAL_Delay(2U);
  } while ((HAL_GetTick() - started) < timeout_ms);

  return timeout_status;
}

static BME280_SENSOR_STATUS read_calibration(void)
{
  uint8_t tp[26];
  uint8_t h[7];

  if ((read_registers(sensor_address, BME280_REG_CALIB_TP,
                      tp, sizeof(tp)) != HAL_OK) ||
      (read_registers(sensor_address, BME280_REG_CALIB_H,
                      h, sizeof(h)) != HAL_OK))
  {
    return BME280_SENSOR_BUS_ERROR;
  }

  calibration.dig_t1 = get_u16_le(&tp[0]);
  calibration.dig_t2 = get_s16_le(&tp[2]);
  calibration.dig_t3 = get_s16_le(&tp[4]);
  calibration.dig_p1 = get_u16_le(&tp[6]);
  calibration.dig_p2 = get_s16_le(&tp[8]);
  calibration.dig_p3 = get_s16_le(&tp[10]);
  calibration.dig_p4 = get_s16_le(&tp[12]);
  calibration.dig_p5 = get_s16_le(&tp[14]);
  calibration.dig_p6 = get_s16_le(&tp[16]);
  calibration.dig_p7 = get_s16_le(&tp[18]);
  calibration.dig_p8 = get_s16_le(&tp[20]);
  calibration.dig_p9 = get_s16_le(&tp[22]);
  calibration.dig_h1 = tp[25];
  calibration.dig_h2 = get_s16_le(&h[0]);
  calibration.dig_h3 = h[2];
  calibration.dig_h4 = sign_extend_12((uint16_t)(((uint16_t)h[3] << 4) |
                                                   (h[4] & 0x0FU)));
  calibration.dig_h5 = sign_extend_12((uint16_t)(((uint16_t)h[5] << 4) |
                                                   (h[4] >> 4)));
  calibration.dig_h6 = (int8_t)h[6];
  calibration.t_fine = 0;

  if ((calibration.dig_t1 == 0U) ||
      (calibration.dig_t1 == UINT16_MAX) ||
      (calibration.dig_p1 == 0U) ||
      (calibration.dig_p1 == UINT16_MAX))
  {
    return BME280_SENSOR_CALIBRATION_ERROR;
  }

  return BME280_SENSOR_OK;
}

static int32_t compensate_temperature(int32_t raw_temperature)
{
  int32_t var1;
  int32_t var2;
  int32_t temperature;

  var1 = (int32_t)((raw_temperature / 8) -
                   ((int32_t)calibration.dig_t1 * 2));
  var1 = (var1 * (int32_t)calibration.dig_t2) / 2048;
  var2 = (int32_t)((raw_temperature / 16) -
                   (int32_t)calibration.dig_t1);
  var2 = (((var2 * var2) / 4096) *
          (int32_t)calibration.dig_t3) / 16384;
  calibration.t_fine = var1 + var2;
  temperature = (calibration.t_fine * 5 + 128) / 256;

  if (temperature < -4000)
  {
    temperature = -4000;
  }
  else if (temperature > 8500)
  {
    temperature = 8500;
  }

  return temperature;
}

static uint32_t compensate_pressure(uint32_t raw_pressure)
{
  int64_t var1;
  int64_t var2;
  int64_t var3;
  int64_t var4;
  uint32_t pressure;

  var1 = (int64_t)calibration.t_fine - 128000;
  var2 = var1 * var1 * (int64_t)calibration.dig_p6;
  var2 += (var1 * (int64_t)calibration.dig_p5) * 131072;
  var2 += (int64_t)calibration.dig_p4 * INT64_C(34359738368);
  var1 = ((var1 * var1 * (int64_t)calibration.dig_p3) / 256) +
         (var1 * (int64_t)calibration.dig_p2 * 4096);
  var3 = INT64_C(140737488355328);
  var1 = ((var3 + var1) * (int64_t)calibration.dig_p1) / INT64_C(8589934592);

  if (var1 == 0)
  {
    return 0U;
  }

  var4 = (int64_t)1048576 - (int64_t)raw_pressure;
  var4 = (((var4 * INT64_C(2147483648)) - var2) * 3125) / var1;
  var1 = ((int64_t)calibration.dig_p9 * (var4 / 8192) *
          (var4 / 8192)) / INT64_C(33554432);
  var2 = ((int64_t)calibration.dig_p8 * var4) / 524288;
  var4 = ((var4 + var1 + var2) / 256) +
         ((int64_t)calibration.dig_p7 * 16);
  pressure = (uint32_t)(((var4 / 2) * 100) / 128);

  if (pressure < 3000000UL)
  {
    pressure = 3000000UL;
  }
  else if (pressure > 11000000UL)
  {
    pressure = 11000000UL;
  }

  return pressure;
}

static uint32_t compensate_humidity(uint16_t raw_humidity)
{
  int32_t var1;
  int32_t var2;
  int32_t var3;
  int32_t var4;
  int32_t var5;
  uint32_t humidity;

  var1 = calibration.t_fine - 76800;
  var2 = (int32_t)raw_humidity * 16384;
  var3 = (int32_t)calibration.dig_h4 * 1048576;
  var4 = (int32_t)calibration.dig_h5 * var1;
  var5 = (((var2 - var3) - var4) + 16384) / 32768;
  var2 = (var1 * (int32_t)calibration.dig_h6) / 1024;
  var3 = (var1 * (int32_t)calibration.dig_h3) / 2048;
  var4 = ((var2 * (var3 + 32768)) / 1024) + 2097152;
  var2 = ((var4 * (int32_t)calibration.dig_h2) + 8192) / 16384;
  var3 = var5 * var2;
  var4 = ((var3 / 32768) * (var3 / 32768)) / 128;
  var5 = var3 - ((var4 * (int32_t)calibration.dig_h1) / 16);

  if (var5 < 0)
  {
    var5 = 0;
  }
  else if (var5 > 419430400)
  {
    var5 = 419430400;
  }

  humidity = (uint32_t)(var5 / 4096);
  if (humidity > 102400UL)
  {
    humidity = 102400UL;
  }
  return humidity;
}

BME280_SENSOR_STATUS BME280_Sensor_Init(void)
{
  static const uint8_t addresses[] =
  {
    BME280_SENSOR_PRIMARY_ADDRESS,
    BME280_SENSOR_SECONDARY_ADDRESS
  };
  BME280_SENSOR_STATUS status;
  uint8_t id;
  uint8_t index;
  uint8_t responding_address = 0U;
  uint8_t responding_id = 0U;

  sensor_initialized = 0U;
  sensor_address = 0U;
  sensor_chip_id = 0U;

  for (index = 0U; index < (uint8_t)(sizeof(addresses) / sizeof(addresses[0]));
       index++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)addresses[index] << 1,
                              3U, BME280_I2C_TIMEOUT_MS) != HAL_OK)
    {
      continue;
    }

    if (read_registers(addresses[index], BME280_REG_CHIP_ID, &id, 1U) != HAL_OK)
    {
      continue;
    }

    responding_address = addresses[index];
    responding_id = id;
    if (id == BME280_SENSOR_CHIP_ID)
    {
      sensor_address = addresses[index];
      sensor_chip_id = id;
      break;
    }
  }

  if (sensor_address == 0U)
  {
    sensor_address = responding_address;
    sensor_chip_id = responding_id;
    return (responding_address != 0U) ? BME280_SENSOR_WRONG_CHIP_ID :
                                       BME280_SENSOR_NOT_FOUND;
  }

  if (write_register(sensor_address, BME280_REG_RESET,
                     BME280_RESET_COMMAND) != HAL_OK)
  {
    return BME280_SENSOR_BUS_ERROR;
  }
  HAL_Delay(3U);

  status = wait_status_clear(BME280_STATUS_IM_UPDATE,
                             BME280_RESET_TIMEOUT_MS,
                             BME280_SENSOR_MEASUREMENT_TIMEOUT);
  if (status != BME280_SENSOR_OK)
  {
    return status;
  }

  status = read_calibration();
  if (status != BME280_SENSOR_OK)
  {
    return status;
  }

  if ((write_register(sensor_address, BME280_REG_CTRL_HUM,
                      BME280_CTRL_HUM_X1) != HAL_OK) ||
      (write_register(sensor_address, BME280_REG_CONFIG,
                      BME280_CONFIG_FILTER_X16) != HAL_OK) ||
      (write_register(sensor_address, BME280_REG_CTRL_MEAS,
                      BME280_CTRL_MEAS_SLEEP) != HAL_OK))
  {
    return BME280_SENSOR_BUS_ERROR;
  }

  sensor_initialized = 1U;
  return BME280_SENSOR_OK;
}

BME280_SENSOR_STATUS BME280_Sensor_Read(BME280_SENSOR_SAMPLE *sample)
{
  uint8_t raw[8];
  uint32_t raw_pressure;
  int32_t raw_temperature;
  uint16_t raw_humidity;
  uint32_t pressure;
  BME280_SENSOR_STATUS status;

  if ((sample == NULL) || (sensor_initialized == 0U))
  {
    return BME280_SENSOR_INVALID_DATA;
  }

  /* ctrl_hum becomes effective when ctrl_meas is written.  Forced mode keeps
     sensor self-heating low and gives one coherent sample per application
     period. */
  if ((write_register(sensor_address, BME280_REG_CTRL_HUM,
                      BME280_CTRL_HUM_X1) != HAL_OK) ||
      (write_register(sensor_address, BME280_REG_CTRL_MEAS,
                      BME280_CTRL_MEAS_FORCED) != HAL_OK))
  {
    return BME280_SENSOR_BUS_ERROR;
  }

  HAL_Delay(2U);
  status = wait_status_clear(BME280_STATUS_MEASURING,
                             BME280_MEASUREMENT_TIMEOUT_MS,
                             BME280_SENSOR_MEASUREMENT_TIMEOUT);
  if (status != BME280_SENSOR_OK)
  {
    return status;
  }

  if (read_registers(sensor_address, BME280_REG_PRESS_MSB,
                     raw, sizeof(raw)) != HAL_OK)
  {
    return BME280_SENSOR_BUS_ERROR;
  }

  raw_pressure = ((uint32_t)raw[0] << 12) |
                 ((uint32_t)raw[1] << 4) |
                 ((uint32_t)raw[2] >> 4);
  raw_temperature = (int32_t)(((uint32_t)raw[3] << 12) |
                              ((uint32_t)raw[4] << 4) |
                              ((uint32_t)raw[5] >> 4));
  raw_humidity = (uint16_t)(((uint16_t)raw[6] << 8) | raw[7]);

  if ((raw_pressure == 0x80000UL) ||
      ((uint32_t)raw_temperature == 0x80000UL))
  {
    return BME280_SENSOR_INVALID_DATA;
  }

  sample->temperature = compensate_temperature(raw_temperature);
  pressure = compensate_pressure(raw_pressure);
  if (pressure == 0U)
  {
    return BME280_SENSOR_INVALID_DATA;
  }
  sample->pressure = pressure;
  sample->humidity = compensate_humidity(raw_humidity);

  return BME280_SENSOR_OK;
}

uint8_t BME280_Sensor_GetAddress(void)
{
  return sensor_address;
}

uint8_t BME280_Sensor_GetChipId(void)
{
  return sensor_chip_id;
}

uint8_t BME280_Sensor_IsInitialized(void)
{
  return sensor_initialized;
}
