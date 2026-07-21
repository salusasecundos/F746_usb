#include "bme280_sensor.h"

#include "i2c.h"
#include "main.h"
#include "tx_api.h"

#include <stddef.h>
#include <string.h>

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

#define BME280_RESET_TIMEOUT_MS          50U
#define BME280_MEASUREMENT_TIMEOUT_MS   100U

/* Used only by the lightweight HAL_I2C_IsDeviceReady() address ping during
   sensor detection; that call has no DMA variant and only runs while
   scanning for the sensor, not on the per-sample hot path. */
#define BME280_I2C_PING_TIMEOUT_MS       50U

/* DMA-completion wait bound for register reads/writes. Unlike the old
   blocking/polling HAL_GetTick()-based timeout, this is not exposed to
   CPU-scheduling jitter (see BME280_THREAD_PRIORITY in app_bme280.c) -- it
   only needs to cover a genuinely stalled/NAKed transaction, so it can be
   generous. */
#define BME280_I2C_DMA_TIMEOUT_MS       200U

#define BME280_I2C_SCL_PIN               GPIO_PIN_8
#define BME280_I2C_SDA_PIN               GPIO_PIN_9
#define BME280_I2C_GPIO_PORT             GPIOB
#define BME280_BUS_RECOVERY_CLOCKS        9U /* worst case: 8 data bits + ACK */
#define BME280_BUS_RECOVERY_DELAY_MS      1U /* ~500 Hz bit-bang clock, plenty
                                                 slow for any I2C slave */

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

/* Recover a stuck I2C1 bus (e.g. a slave left holding SDA low across a
   sensor reset or an ESD glitch) using the standard I2C-bus specification
   recovery procedure (UM10204 section 3.1.16): manually clock SCL up to
   9 times while SDA is released, watching for SDA to go high, then issue a
   STOP condition. This runs every time the sensor is (re)initialized; on a
   healthy bus SDA already reads high and the clock loop exits immediately,
   so it is a cheap no-op in the common case. */
static void bus_recover(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t clock;

  HAL_I2C_DeInit(&hi2c1);

  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin = BME280_I2C_SCL_PIN;
  HAL_GPIO_Init(BME280_I2C_GPIO_PORT, &gpio);
  gpio.Pin = BME280_I2C_SDA_PIN;
  HAL_GPIO_Init(BME280_I2C_GPIO_PORT, &gpio);

  HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT,
                    BME280_I2C_SCL_PIN | BME280_I2C_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);

  for (clock = 0U; clock < BME280_BUS_RECOVERY_CLOCKS; clock++)
  {
    if (HAL_GPIO_ReadPin(BME280_I2C_GPIO_PORT, BME280_I2C_SDA_PIN) ==
        GPIO_PIN_SET)
    {
      break;
    }
    HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT, BME280_I2C_SCL_PIN,
                      GPIO_PIN_RESET);
    HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);
    HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT, BME280_I2C_SCL_PIN,
                      GPIO_PIN_SET);
    HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);
  }

  /* STOP condition: SDA rises while SCL is high. */
  HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT, BME280_I2C_SDA_PIN, GPIO_PIN_RESET);
  HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);
  HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT, BME280_I2C_SCL_PIN, GPIO_PIN_SET);
  HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);
  HAL_GPIO_WritePin(BME280_I2C_GPIO_PORT, BME280_I2C_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(BME280_BUS_RECOVERY_DELAY_MS);

  /* Hand the pins back to the I2C peripheral (re-applies AF_OD config via
     HAL_I2C_MspInit) and bring the peripheral back up. */
  (void)MX_I2C1_Init();
}

/* Largest single transfer is the 26-byte T/P calibration block; round up to
   one 32-byte Cortex-M7 D-Cache line. Statically allocated and aligned so
   the manual cache maintenance below only ever touches memory owned
   exclusively by this buffer -- the D-Cache is enabled (SCB_EnableDCache()
   in main.c) and neither the I2C nor the DMA HAL layers maintain cache
   coherency for you. Used only from the single BME280 thread, so no
   locking is needed around it. */
#define BME280_DMA_BUFFER_SIZE           32U
static uint8_t i2c1_dma_buffer[BME280_DMA_BUFFER_SIZE]
    __attribute__((aligned(32)));

static TX_SEMAPHORE i2c1_dma_done;
static uint8_t i2c1_dma_ready;
static volatile uint8_t i2c1_dma_ok;

static void i2c1_dma_ensure_ready(void)
{
  if (i2c1_dma_ready == 0U)
  {
    if (tx_semaphore_create(&i2c1_dma_done, "BME280 I2C1 DMA", 0U) ==
        TX_SUCCESS)
    {
      i2c1_dma_ready = 1U;
    }
  }
}

static uint32_t ms_to_ticks(uint32_t milliseconds)
{
  uint32_t ticks = (milliseconds * TX_TIMER_TICKS_PER_SECOND) / 1000U;
  return (ticks != 0U) ? ticks : 1U;
}

/* These override the HAL's weak defaults for every I2C instance, so they
   must filter on hi2c->Instance: I2C3 (touch, app_guix.c) still uses the
   plain blocking HAL_I2C_Mem_Read() and never triggers these. */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    i2c1_dma_ok = 1U;
    (void)tx_semaphore_put(&i2c1_dma_done);
  }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    i2c1_dma_ok = 1U;
    (void)tx_semaphore_put(&i2c1_dma_done);
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1)
  {
    i2c1_dma_ok = 0U;
    (void)tx_semaphore_put(&i2c1_dma_done);
  }
}

static HAL_StatusTypeDef read_registers(uint8_t address, uint8_t reg,
                                        uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef status;

  if (length > BME280_DMA_BUFFER_SIZE)
  {
    return HAL_ERROR;
  }

  i2c1_dma_ensure_ready();
  /* Drop a stale completion post left over from a previous timed-out
     transfer so it can't be mistaken for this one's. */
  (void)tx_semaphore_get(&i2c1_dma_done, TX_NO_WAIT);

  status = HAL_I2C_Mem_Read_DMA(&hi2c1, (uint16_t)address << 1, reg,
                                I2C_MEMADD_SIZE_8BIT, i2c1_dma_buffer,
                                length);
  if (status != HAL_OK)
  {
    return status;
  }

  if (tx_semaphore_get(&i2c1_dma_done,
                       ms_to_ticks(BME280_I2C_DMA_TIMEOUT_MS)) != TX_SUCCESS)
  {
    return HAL_TIMEOUT;
  }
  if (i2c1_dma_ok == 0U)
  {
    return HAL_ERROR;
  }

  SCB_InvalidateDCache_by_Addr((uint32_t *)(uint32_t)i2c1_dma_buffer,
                               (int32_t)BME280_DMA_BUFFER_SIZE);
  (void)memcpy(data, i2c1_dma_buffer, length);
  return HAL_OK;
}

static HAL_StatusTypeDef write_register(uint8_t address, uint8_t reg,
                                        uint8_t value)
{
  HAL_StatusTypeDef status;

  i2c1_dma_ensure_ready();
  (void)tx_semaphore_get(&i2c1_dma_done, TX_NO_WAIT);

  i2c1_dma_buffer[0] = value;
  SCB_CleanDCache_by_Addr((uint32_t *)(uint32_t)i2c1_dma_buffer,
                          (int32_t)BME280_DMA_BUFFER_SIZE);

  status = HAL_I2C_Mem_Write_DMA(&hi2c1, (uint16_t)address << 1, reg,
                                 I2C_MEMADD_SIZE_8BIT, i2c1_dma_buffer, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  if (tx_semaphore_get(&i2c1_dma_done,
                       ms_to_ticks(BME280_I2C_DMA_TIMEOUT_MS)) != TX_SUCCESS)
  {
    return HAL_TIMEOUT;
  }

  return (i2c1_dma_ok != 0U) ? HAL_OK : HAL_ERROR;
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

  /* Bit-for-bit port of Bosch's official BME280_compensate_T_int32()
     reference (datasheet section 4.2.3). The intermediate terms can be
     negative (cold readings, negative dig_T2/dig_T3), so this must use
     arithmetic right shifts, not '/', which truncates toward zero instead
     of flooring and would bias the result by up to 1 LSB per stage. */
  var1 = (int32_t)((raw_temperature >> 3) -
                   ((int32_t)calibration.dig_t1 << 1));
  var1 = (var1 * (int32_t)calibration.dig_t2) >> 11;
  var2 = (int32_t)((raw_temperature >> 4) -
                   (int32_t)calibration.dig_t1);
  var2 = (((var2 * var2) >> 12) *
          (int32_t)calibration.dig_t3) >> 14;
  calibration.t_fine = var1 + var2;
  temperature = (calibration.t_fine * 5 + 128) >> 8;

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

  /* Bit-for-bit port of Bosch's official bme280_compensate_P_int64()
     reference (datasheet section 4.2.3). Divisions by a power of two on
     terms that can be negative are replaced with '>>' to match the
     reference's flooring behaviour (see compensate_temperature() above);
     multiplications by a power of two are left as-is since they are exact
     either way. */
  var1 = (int64_t)calibration.t_fine - 128000;
  var2 = var1 * var1 * (int64_t)calibration.dig_p6;
  var2 += (var1 * (int64_t)calibration.dig_p5) * 131072;
  var2 += (int64_t)calibration.dig_p4 * INT64_C(34359738368);
  var1 = ((var1 * var1 * (int64_t)calibration.dig_p3) >> 8) +
         (var1 * (int64_t)calibration.dig_p2 * 4096);
  var3 = INT64_C(140737488355328);
  var1 = ((var3 + var1) * (int64_t)calibration.dig_p1) >> 33;

  if (var1 == 0)
  {
    return 0U;
  }

  var4 = (int64_t)1048576 - (int64_t)raw_pressure;
  var4 = (((var4 * INT64_C(2147483648)) - var2) * 3125) / var1;
  var1 = ((int64_t)calibration.dig_p9 * (var4 >> 13) *
          (var4 >> 13)) >> 25;
  var2 = ((int64_t)calibration.dig_p8 * var4) >> 19;
  var4 = ((var4 + var1 + var2) >> 8) +
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

  /* Bit-for-bit port of Bosch's official bme280_compensate_H_int32()
     reference (datasheet section 4.2.3); see compensate_temperature() above
     for why '>>' replaces '/' on terms that can go negative. */
  var1 = calibration.t_fine - 76800;
  var2 = (int32_t)raw_humidity * 16384;
  var3 = (int32_t)calibration.dig_h4 * 1048576;
  var4 = (int32_t)calibration.dig_h5 * var1;
  var5 = (((var2 - var3) - var4) + 16384) >> 15;
  var2 = (var1 * (int32_t)calibration.dig_h6) >> 10;
  var3 = (var1 * (int32_t)calibration.dig_h3) >> 11;
  var4 = ((var2 * (var3 + 32768)) >> 10) + 2097152;
  var2 = ((var4 * (int32_t)calibration.dig_h2) + 8192) >> 14;
  var3 = var5 * var2;
  var4 = ((var3 >> 15) * (var3 >> 15)) >> 7;
  var5 = var3 - ((var4 * (int32_t)calibration.dig_h1) >> 4);

  if (var5 < 0)
  {
    var5 = 0;
  }
  else if (var5 > 419430400)
  {
    var5 = 419430400;
  }

  humidity = (uint32_t)(var5 >> 12);
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

  bus_recover();

  for (index = 0U; index < (uint8_t)(sizeof(addresses) / sizeof(addresses[0]));
       index++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)addresses[index] << 1,
                              3U, BME280_I2C_PING_TIMEOUT_MS) != HAL_OK)
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
