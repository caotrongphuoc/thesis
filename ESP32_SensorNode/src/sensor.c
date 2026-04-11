#include "sensor.h"
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

static const char *TAG = "SENSOR";

static esp_adc_cal_characteristics_t adc_chars;
float Ro = 10.0f;
float rs_buffer[10] = {0};
int rs_index = 0;

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read(void);
#ifdef __cplusplus
}
#endif

void sensors_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(MQ135_ADC_CHANNEL, ADC_ATTEN_DB_12);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    ESP_LOGI(TAG, "Sensors initialized (HTU21 + MQ135)");
}

static uint16_t htu21_read_raw(uint8_t cmd)
{
    uint8_t data[2];
    i2c_master_write_to_device(I2C_MASTER_NUM, HTU21_ADDR, &cmd, 1, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_master_read_from_device(I2C_MASTER_NUM, HTU21_ADDR, data, 2, pdMS_TO_TICKS(100));
    return (data[0] << 8) | data[1];
}

float get_temp(void)
{
    uint16_t raw = htu21_read_raw(CMD_TEMP);
    return -46.85f + 175.72f * raw / 65536.0f;
}

float get_hum(void)
{
    uint16_t raw = htu21_read_raw(CMD_HUM);
    return -6.0f + 125.0f * raw / 65536.0f;
}

float get_mcu_temp(void)
{
    uint8_t raw = temprature_sens_read();
    return (raw - 32) / 1.8f;
}

static float get_mq135_rs(void)
{
    int raw = adc1_get_raw(MQ135_ADC_CHANNEL);
    uint32_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
    float v_out = mv / 1000.0f;
    if (v_out < 0.2f || v_out > 3.2f) return 999.0f;
    return ((3.3f - v_out) / v_out) * RL;
}

float get_rs_avg(void)
{
    float rs = get_mq135_rs();
    rs_buffer[rs_index] = rs;
    rs_index = (rs_index + 1) % 10;

    float sum = 0;
    for (int i = 0; i < 10; i++) sum += rs_buffer[i];
    return sum / 10.0f;
}

float get_co2_ppm(void)
{
    float rs = get_rs_avg();
    float ratio = (Ro > 0.0f) ? rs / Ro : 1.0f;
    return ATMOCO2 * powf(ratio / CLEAN_AIR_RATIO, -0.42f);
}

static float get_correction_factor(float t, float h)
{
    return (1.0f + 0.0008f * (h - 65.0f)) * (1.0f - 0.003f * (t - 25.0f));
}

void mq135_calibrate(float t, float h)
{
    ESP_LOGI(TAG, "=== Calibrating MQ135 Ro (60s) ===");
    vTaskDelay(pdMS_TO_TICKS(60000));

    float rs_sum = 0;
    for (int i = 0; i < 100; i++) {
        rs_sum += get_mq135_rs();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    float rs_avg = rs_sum / 100.0f;
    float corr = get_correction_factor(t, h);
    Ro = rs_avg / (CLEAN_AIR_RATIO * corr);

    ESP_LOGI(TAG, "Calibration done! Ro = %.2f kOhm", Ro);
}

float get_voltage(void)
{
    uint32_t adc_reading = 0;
    for (int i = 0; i < 10; i++) {
        adc_reading += adc1_get_raw(MQ135_ADC_CHANNEL);
    }
    adc_reading /= 10;
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    return voltage_mv / 1000.0f;
}