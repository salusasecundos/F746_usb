#ifndef BME280_SENSOR_H
#define BME280_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define BME280_SENSOR_CHIP_ID              0x60U
#define BME280_SENSOR_PRIMARY_ADDRESS      0x76U
#define BME280_SENSOR_SECONDARY_ADDRESS    0x77U

/* The scales intentionally match the legacy F4 firmware and its 64-byte
   USB/LAN protocol. */
#define BME280_TEMPERATURE_SCALE           100L
#define BME280_PRESSURE_SCALE              100UL
#define BME280_HUMIDITY_SCALE              1024UL

typedef enum
{
  BME280_SENSOR_OK = 0,
  BME280_SENSOR_NOT_FOUND,
  BME280_SENSOR_WRONG_CHIP_ID,
  BME280_SENSOR_BUS_ERROR,
  BME280_SENSOR_CALIBRATION_ERROR,
  BME280_SENSOR_MEASUREMENT_TIMEOUT,
  BME280_SENSOR_INVALID_DATA
} BME280_SENSOR_STATUS;

typedef struct
{
  int32_t temperature; /* degrees Celsius x 100 */
  uint32_t pressure;   /* pascals x 100 */
  uint32_t humidity;   /* percent relative humidity x 1024 */
} BME280_SENSOR_SAMPLE;

BME280_SENSOR_STATUS BME280_Sensor_Init(void);
BME280_SENSOR_STATUS BME280_Sensor_Read(BME280_SENSOR_SAMPLE *sample);
uint8_t BME280_Sensor_GetAddress(void);
uint8_t BME280_Sensor_GetChipId(void);
uint8_t BME280_Sensor_IsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* BME280_SENSOR_H */
