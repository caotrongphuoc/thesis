#ifndef _SENSOR_H_
#define _SENSOR_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   100000

#define MQ135_ADC_CHANNEL    ADC1_CHANNEL_6
#define HTU21_ADDR           0x40
#define CMD_TEMP             0xF3
#define CMD_HUM              0xF5

#define RL                   1.0f
#define CLEAN_AIR_RATIO      3.6f
#define ATMOCO2              420.0f

extern float Ro;

void sensors_init(void);
float get_temp(void);
float get_hum(void);
float get_mcu_temp(void);
float get_rs_avg(void);
float get_co2_ppm(void);
float get_voltage(void);
void mq135_calibrate(float t, float h);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_H_ */