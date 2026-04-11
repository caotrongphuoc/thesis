#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"
#include "esp_ble_mesh_health_model_api.h"

#include "ble_mesh_example_init.h"
#include "sensor.h"

#define TAG "SENSOR_NODE"

#define SENSOR_TEMP_PROPERTY_ID     0x004F
#define SENSOR_HUM_PROPERTY_ID      0x0052
#define SENSOR_CO2_PROPERTY_ID      0x0060
#define SENSOR_COUNT                3

#define SENSOR_POSITIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_POS_TOLERANCE
#define SENSOR_NEGATIVE_TOLERANCE   ESP_BLE_MESH_SENSOR_UNSPECIFIED_NEG_TOLERANCE
#define SENSOR_SAMPLE_FUNCTION      ESP_BLE_MESH_SAMPLE_FUNC_UNSPECIFIED
#define SENSOR_MEASURE_PERIOD       ESP_BLE_MESH_SENSOR_NOT_APPL_MEASURE_PERIOD
#define SENSOR_UPDATE_INTERVAL      ESP_BLE_MESH_SENSOR_NOT_APPL_UPDATE_INTERVAL

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_temp, 2);
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_hum,  2);
NET_BUF_SIMPLE_DEFINE_STATIC(sensor_data_co2,  2);

static esp_ble_mesh_sensor_state_t sensor_states[SENSOR_COUNT] = {
    [0] = {
        .sensor_property_id = SENSOR_TEMP_PROPERTY_ID,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function  = SENSOR_SAMPLE_FUNCTION,
            .measure_period     = SENSOR_MEASURE_PERIOD,
            .update_interval    = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format    = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length    = 2,
            .raw_value = &sensor_data_temp,
        },
    },
    [1] = {
        .sensor_property_id = SENSOR_HUM_PROPERTY_ID,
        .descriptor = {
            .positive_tolerance = SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function  = SENSOR_SAMPLE_FUNCTION,
            .measure_period     = SENSOR_MEASURE_PERIOD,
            .update_interval    = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format    = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length    = 1,
            .raw_value = &sensor_data_hum,
        },
    },
    [2] = {
        .sensor_property_id = SENSOR_CO2_PROPERTY_ID,
        .descriptor = {
            .positive_tolerance = 0x64,
            .negative_tolerance = 0x64,
            .sampling_function  = SENSOR_SAMPLE_FUNCTION,
            .measure_period     = SENSOR_MEASURE_PERIOD,
            .update_interval    = SENSOR_UPDATE_INTERVAL,
        },
        .sensor_data = {
            .format    = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A,
            .length    = 2,
            .raw_value = &sensor_data_co2,
        },
    },
};

static const uint8_t health_test_ids[] = { ESP_BLE_MESH_HEALTH_STANDARD_TEST };
static esp_ble_mesh_health_srv_t health_server = {
    .health_test = {
        .id_count   = 1,
        .test_ids   = health_test_ids,
        .company_id = 0x02E5,
    },
};
ESP_BLE_MESH_HEALTH_PUB_DEFINE(health_pub, 0, ROLE_NODE);

ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_pub, 40, ROLE_NODE);
static esp_ble_mesh_sensor_srv_t sensor_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = SENSOR_COUNT,
    .states      = sensor_states,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(sensor_setup_pub, 40, ROLE_NODE);
static esp_ble_mesh_sensor_setup_srv_t sensor_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
    .state_count = SENSOR_COUNT,
    .states      = sensor_states,
};

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl      = 7,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&health_server, &health_pub),
    ESP_BLE_MESH_MODEL_SENSOR_SRV(&sensor_pub, &sensor_server),
    ESP_BLE_MESH_MODEL_SENSOR_SETUP_SRV(&sensor_setup_pub, &sensor_setup_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid           = 0x02E5,
    .element_count = ARRAY_SIZE(elements),
    .elements      = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

#define SENSOR_STATUS_BUF_LEN   16

static uint16_t build_sensor_status(uint8_t *buf, uint16_t buf_len)
{
    if (!buf || buf_len < SENSOR_STATUS_BUF_LEN) return 0;
    uint16_t offset = 0;

    for (int i = 0; i < SENSOR_COUNT; i++) {
        esp_ble_mesh_sensor_state_t *state = &sensor_states[i];
        uint16_t prop_id  = state->sensor_property_id;
        uint8_t  data_len = state->sensor_data.raw_value->len;
        if (data_len == 0) continue;

        uint16_t mpid = ESP_BLE_MESH_SENSOR_DATA_FORMAT_A_MPID(data_len, prop_id);
        buf[offset++] = (uint8_t)(mpid & 0xFF);
        buf[offset++] = (uint8_t)(mpid >> 8);
        memcpy(buf + offset, state->sensor_data.raw_value->data, data_len);
        offset += data_len;
    }
    return offset;
}

static void update_sensor_values(void)
{
    float t   = get_temp();
    float h   = get_hum();
    float co2 = get_co2_ppm();
    float mcu = get_mcu_temp();

    // Encode temp: nhân 100 để giữ 2 số thập phân, dùng 2 bytes
    uint16_t temp_raw = (uint16_t)((t + 64.0f) * 100.0f);
    // Encode hum: nhân 100, dùng 2 bytes
    uint16_t hum_raw  = (uint16_t)(h * 100.0f);
    // CO2: giữ nguyên uint16
    uint16_t co2_raw  = (uint16_t)co2;

    net_buf_simple_reset(&sensor_data_temp);
    net_buf_simple_add_le16(&sensor_data_temp, temp_raw);

    net_buf_simple_reset(&sensor_data_hum);
    net_buf_simple_add_le16(&sensor_data_hum, hum_raw);

    net_buf_simple_reset(&sensor_data_co2);
    net_buf_simple_add_le16(&sensor_data_co2, co2_raw);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Temp      : %.2f C (raw=%d)", t, temp_raw);
    ESP_LOGI(TAG, "  Humidity  : %.2f %% (raw=%d)", h, hum_raw);
    ESP_LOGI(TAG, "  CO2/VOC   : %.2f ppm (raw=%d)", co2, co2_raw);
    ESP_LOGI(TAG, "  MCU Temp  : %.2f C", mcu);
    ESP_LOGI(TAG, "========================================");
}

static void publish_sensor_status(void)
{
    uint8_t buf[SENSOR_STATUS_BUF_LEN] = {0};
    uint16_t len = build_sensor_status(buf, sizeof(buf));

    if (len == 0) {
        ESP_LOGW(TAG, "Sensor data not ready");
        return;
    }

    esp_err_t err = esp_ble_mesh_model_publish(
        &root_models[2],
        ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS,
        len, buf, ROLE_NODE);

    if (err != ESP_OK)
        ESP_LOGW(TAG, "Publish failed (err %d)", err);
    else
        ESP_LOGI(TAG, "Published sensor status (%d bytes)", len);
}

static void sensor_update_task(void *pvParameters)
{
    while (1) {
        update_sensor_values();
        publish_sensor_status();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "Provisioning complete! Addr: 0x%04x", addr);
    update_sensor_values();

    // Set publication address to group 0xC000
    sensor_pub.publish_addr = 0xC000;
    sensor_pub.app_idx = 0x0000;
    sensor_pub.ttl = 7;
    sensor_pub.period = 0;
}

static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                         esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        prov_complete(param->node_prov_complete.net_idx,
                      param->node_prov_complete.addr,
                      param->node_prov_complete.flags,
                      param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "Node reset");
        esp_ble_mesh_node_local_reset();
        break;
    default:
        break;
    }
}

static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                  esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "AppKey added"); break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "Model bound to AppKey"); break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGI(TAG, "Subscription added"); break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET:
            ESP_LOGI(TAG, "Publication set"); break;
        default: break;
        }
    }
}

static void mesh_sensor_server_cb(esp_ble_mesh_sensor_server_cb_event_t event,
                                  esp_ble_mesh_sensor_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_SENSOR_SERVER_RECV_GET_MSG_EVT &&
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_SENSOR_GET) {

        ESP_LOGI(TAG, "Received GET request");
        update_sensor_values();

        uint8_t buf[SENSOR_STATUS_BUF_LEN] = {0};
        uint16_t len = build_sensor_status(buf, sizeof(buf));

        if (len > 0) {
            esp_ble_mesh_server_model_send_msg(param->model, &param->ctx,
                ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS, len, buf);
        }
    }
}

static void mesh_health_server_cb(esp_ble_mesh_health_server_cb_event_t event,
                                  esp_ble_mesh_health_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "Health event: %d", event);
}

static esp_err_t ble_mesh_init(void)
{
    esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    esp_ble_mesh_register_sensor_server_callback(mesh_sensor_server_cb);
    esp_ble_mesh_register_health_server_callback(mesh_health_server_cb);

    ESP_ERROR_CHECK(esp_ble_mesh_init(&provision, &composition));
    ESP_ERROR_CHECK(esp_ble_mesh_node_prov_enable(
        ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));

    ESP_LOGI(TAG, "BLE Mesh Sensor Node initialized");
    ESP_LOGI(TAG, "Waiting for provisioning...");
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "=== BLE Mesh Sensor Node Starting ===");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    sensors_init();

    ESP_LOGI(TAG, "MQ135 preheat 30s...");
    vTaskDelay(pdMS_TO_TICKS(30000));

    float t = get_temp();
    float h = get_hum();
    ESP_LOGI(TAG, "Initial: Temp=%.2f C | Hum=%.2f %%", t, h);

    mq135_calibrate(t, h);
    update_sensor_values();

    ESP_LOGI(TAG, "Initializing Bluetooth...");
    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth init failed");
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);
    err = ble_mesh_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh init failed");
        return;
    }

    xTaskCreate(sensor_update_task, "sensor_update", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== READY! Waiting for provision ===");
}