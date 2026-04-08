#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

// Đọc nhiệt độ nội bộ ESP32
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

// ==================== CẤU HÌNH ====================
#define WIFI_SSID      "TP-Link_385B"
#define WIFI_PASS      "91324566"
#define MQTT_BROKER    "mqtt://192.168.1.113"
#define MQTT_TOPIC     "ble/node/1/sensor"
#define NODE_ID        1

// I2C + HTU21
#define I2C_MASTER_SCL_IO   22
#define I2C_MASTER_SDA_IO   21
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  100000
#define HTU21_ADDR  0x40
#define CMD_TEMP    0xF3
#define CMD_HUM     0xF5

// ADC - Voltage trên GPIO34
#define ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_WIDTH       ADC_WIDTH_BIT_12

// ==================== BIẾN TOÀN CỤC ====================
static const char *TAG = "BLE_MESH_NODE";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static esp_adc_cal_characteristics_t adc_chars;

// ==================== HTU21 SENSOR ====================
void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

uint16_t read_sensor(uint8_t cmd) {
    uint8_t data[2];
    i2c_master_write_to_device(I2C_MASTER_NUM, HTU21_ADDR, &cmd, 1, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_master_read_from_device(I2C_MASTER_NUM, HTU21_ADDR, data, 2, pdMS_TO_TICKS(100));
    return (data[0] << 8) | data[1];
}

float read_temperature() {
    uint16_t raw = read_sensor(CMD_TEMP);
    return -46.85 + 175.72 * raw / 65536.0;
}

float read_humidity() {
    uint16_t raw = read_sensor(CMD_HUM);
    return -6 + 125.0 * raw / 65536.0;
}

// ==================== MCU TEMPERATURE ====================
float read_mcu_temperature() {
    uint8_t raw = temprature_sens_read();
    return (raw - 32) / 1.8;  // Fahrenheit to Celsius
}

// ==================== ADC VOLTAGE ====================
void adc_init() {
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
}

float read_voltage() {
    uint32_t adc_reading = 0;
    // Trung bình 10 lần đọc cho ổn định
    for (int i = 0; i < 10; i++) {
        adc_reading += adc1_get_raw(ADC_CHANNEL);
    }
    adc_reading /= 10;

    // Chuyển sang mV
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    return voltage_mv / 1000.0;  // mV -> V
}

// ==================== WIFI ====================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi: %s ...", WIFI_SSID);
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// ==================== MQTT ====================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker!");
            mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

void mqtt_init(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s ...", MQTT_BROKER);
}

// ==================== PUBLISH ====================
void publish_sensor_data(float temp, float hum, float mcu_temp, float volt) {
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, skipping publish");
        return;
    }

    char json[200];
    snprintf(json, sizeof(json),
             "{\"node\":%d,\"temp\":%.2f,\"hum\":%.2f,\"mcu_temp\":%.2f,\"volt\":%.2f}",
             NODE_ID, temp, hum, mcu_temp, volt);

    int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json, 0, 1, 0);
    ESP_LOGI(TAG, "Published [%s]: %s (msg_id=%d)", MQTT_TOPIC, json, msg_id);
}

// ==================== MAIN ====================
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo
    i2c_master_init();
    adc_init();
    ESP_LOGI(TAG, "Sensors initialized (HTU21 + MCU Temp + ADC Voltage)");

    wifi_init();
    mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        float temp = read_temperature();
        float hum  = read_humidity();
        float mcu_temp = read_mcu_temperature();
        float volt = read_voltage();

        printf("Temp: %.2f C | Hum: %.2f %% | MCU: %.2f C | Volt: %.2f V\n",
               temp, hum, mcu_temp, volt);

        publish_sensor_data(temp, hum, mcu_temp, volt);

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}