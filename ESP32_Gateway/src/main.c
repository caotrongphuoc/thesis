/* Gateway - BLE Mesh Provisioner + Sensor Client + WiFi/MQTT */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_sensor_model_api.h"

#include "ble_mesh_example_init.h"

#define TAG "GATEWAY"

#define WIFI_SSID       "TP-Link_385B"
#define WIFI_PASS       "91324566"
#define MQTT_BROKER     "mqtt://192.168.1.113"

#define SENSOR_TEMP_PROPERTY_ID     0x004F
#define SENSOR_HUM_PROPERTY_ID      0x0052
#define SENSOR_CO2_PROPERTY_ID      0x0060

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

static uint16_t provisioned_addr[4] = {0};
static int provisioned_count = 0;
static uint16_t app_key_idx = 0x0000;
static uint16_t net_key_idx = 0x0000;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10, 0xFF, 0xFF };

// ==================== WIFI ====================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &got_ip));

    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

// ==================== MQTT ====================
static void mqtt_event_handler(void *args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: mqtt_connected = true; ESP_LOGI(TAG, "MQTT connected!"); break;
        case MQTT_EVENT_DISCONNECTED: mqtt_connected = false; ESP_LOGW(TAG, "MQTT disconnected"); break;
        default: break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = { .broker.address.uri = MQTT_BROKER };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void publish_sensor_to_mqtt(int node_id, float temp, float hum, float co2)
{
    if (!mqtt_connected) return;
    char topic[64], json[256];
    float aqi = co2 < 700 ? co2 / 10.0f : 50.0f + (co2 - 700) / 20.0f;
    snprintf(topic, sizeof(topic), "ble/node/%d/sensor", node_id);
    snprintf(json, sizeof(json),
             "{\"node\":%d,\"temp\":%.2f,\"hum\":%.2f,\"mcu_temp\":0.00,\"volt\":0.00,\"voc\":%.2f,\"aqi\":%.2f}",
             node_id, temp, hum, co2, aqi);
    esp_mqtt_client_publish(mqtt_client, topic, json, 0, 1, 0);
    ESP_LOGI(TAG, "MQTT [%s]: %s", topic, json);
}

static void parse_sensor_status(uint8_t *data, uint16_t len, uint16_t src_addr)
{
    if (!data || len < 3) return;

    float temp = 0, hum = 0, co2 = 0;
    int node_id = 1;

    for (int i = 0; i < provisioned_count; i++) {
        if (provisioned_addr[i] == src_addr) { node_id = i + 1; break; }
    }

    // Parse theo vị trí cố định từ raw data
    // Format: [MPID_temp 2B][temp_data 2B][MPID_hum 2B][hum_data 2B][MPID_co2 2B][co2_data 2B]
    // Total = 12 bytes

    if (len >= 12) {
        // Temp: byte 2-3 (little endian)
        uint16_t temp_raw = data[2] | (data[3] << 8);
        temp = (temp_raw / 100.0f) - 64.0f;

        // Hum: byte 6-7 (little endian)
        uint16_t hum_raw = data[6] | (data[7] << 8);
        hum = hum_raw / 100.0f;

        // CO2: byte 10-11 (little endian)
        uint16_t co2_raw = data[10] | (data[11] << 8);
        co2 = (float)co2_raw;
    }
    else if (len >= 4) {
        uint16_t temp_raw = data[2] | (data[3] << 8);
        temp = (temp_raw / 100.0f) - 64.0f;
    }

    ESP_LOGI(TAG, "Node %d: Temp=%.2f Hum=%.2f CO2=%.2f", node_id, temp, hum, co2);
    publish_sensor_to_mqtt(node_id, temp, hum, co2);
}

// ==================== BLE MESH MODELS ====================
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit     = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay            = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon           = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy       = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state     = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl      = 7,
};

static esp_ble_mesh_client_t config_client;
static esp_ble_mesh_client_t sensor_client;

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_SENSOR_CLI(NULL, &sensor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = 0x02E5,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .prov_unicast_addr = 0x0001,
    .prov_start_address = 0x0005,
};

// ==================== CONFIGURE NODE ====================
static void configure_node(uint16_t addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    common.opcode = ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD;
    common.model = &root_models[1];  // Config Client model
    common.ctx.net_idx = net_key_idx;
    common.ctx.app_idx = 0xFFFF;
    common.ctx.addr = addr;
    common.ctx.send_ttl = 7;
    common.msg_timeout = 5000;

    esp_ble_mesh_cfg_client_set_state_t set = {0};
    set.app_key_add.net_idx = net_key_idx;
    set.app_key_add.app_idx = app_key_idx;

    const uint8_t app_key[16] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                  0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    memcpy(set.app_key_add.app_key, app_key, 16);

    esp_ble_mesh_config_client_set_state(&common, &set);
    ESP_LOGI(TAG, "Sending AppKey to node 0x%04x", addr);
}

static void bind_sensor_model(uint16_t addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND;
    common.model = &root_models[1];
    common.ctx.net_idx = net_key_idx;
    common.ctx.app_idx = 0xFFFF;
    common.ctx.addr = addr;
    common.ctx.send_ttl = 7;
    common.msg_timeout = 5000;

    esp_ble_mesh_cfg_client_set_state_t set = {0};
    set.model_app_bind.element_addr = addr;
    set.model_app_bind.model_app_idx = app_key_idx;
    set.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_SENSOR_SRV;
    set.model_app_bind.company_id = 0xFFFF;

    esp_ble_mesh_config_client_set_state(&common, &set);
    ESP_LOGI(TAG, "Binding Sensor Model on node 0x%04x", addr);
}

static void set_node_publication(uint16_t addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    common.opcode = ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET;
    common.model = &root_models[1];
    common.ctx.net_idx = net_key_idx;
    common.ctx.app_idx = 0xFFFF;
    common.ctx.addr = addr;
    common.ctx.send_ttl = 7;
    common.msg_timeout = 5000;

    esp_ble_mesh_cfg_client_set_state_t set = {0};
    set.model_pub_set.element_addr = addr;
    set.model_pub_set.publish_addr = 0xC000;
    set.model_pub_set.publish_app_idx = app_key_idx;
    set.model_pub_set.publish_ttl = 7;
    set.model_pub_set.publish_period = 0x45;  // 5 seconds
    set.model_pub_set.model_id = ESP_BLE_MESH_MODEL_ID_SENSOR_SRV;
    set.model_pub_set.company_id = 0xFFFF;

    esp_ble_mesh_config_client_set_state(&common, &set);
    ESP_LOGI(TAG, "Setting publication for node 0x%04x to group 0xC000", addr);
}

// ==================== PROVISIONER CALLBACK ====================
static void mesh_prov_cb(esp_ble_mesh_prov_cb_event_t event,
                         esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner registered");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "Provisioner scan enabled");
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
        ESP_LOGI(TAG, "Found unprovisioned device!");
        esp_ble_mesh_unprov_dev_add_t dev = {0};
        memcpy(dev.uuid, param->provisioner_recv_unprov_adv_pkt.dev_uuid, 16);
        dev.addr_type = param->provisioner_recv_unprov_adv_pkt.addr_type;
        memcpy(dev.addr, param->provisioner_recv_unprov_adv_pkt.addr, 6);
        dev.bearer = ESP_BLE_MESH_PROV_ADV;
        esp_ble_mesh_provisioner_add_unprov_dev(&dev,
            ADD_DEV_FLUSHABLE_DEV_FLAG | ADD_DEV_START_PROV_NOW_FLAG);
        break;
    }
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT: {
        uint16_t addr = param->provisioner_prov_complete.unicast_addr;
        ESP_LOGI(TAG, "=== Provisioning complete! Node addr: 0x%04x ===", addr);
        if (provisioned_count < 4) {
            provisioned_addr[provisioned_count++] = addr;
            ESP_LOGI(TAG, "Node %d registered (total: %d)", provisioned_count, provisioned_count);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        configure_node(addr);
        break;
    }
    default:
        break;
    }
}

// ==================== CONFIG CLIENT CALLBACK ====================
static void mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                  esp_ble_mesh_cfg_client_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            ESP_LOGI(TAG, "AppKey added to node 0x%04x (err: %d)",
                     param->params->ctx.addr, param->error_code);
            if (param->error_code == 0) {
                bind_sensor_model(param->params->ctx.addr);
            }
        }
        else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGI(TAG, "Sensor Model bound on node 0x%04x", param->params->ctx.addr);
            set_node_publication(param->params->ctx.addr);
        }
    }
}

// ==================== CONFIG SERVER CALLBACK ====================
static void mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                  esp_ble_mesh_cfg_server_cb_param_t *param)
{
    ESP_LOGI(TAG, "Config server event: %d", event);
}

// ==================== SENSOR CLIENT CALLBACK ====================
static void mesh_sensor_client_cb(esp_ble_mesh_sensor_client_cb_event_t event,
                                  esp_ble_mesh_sensor_client_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_SENSOR_CLIENT_GET_STATE_EVT ||
        event == ESP_BLE_MESH_SENSOR_CLIENT_PUBLISH_EVT) {

        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_SENSOR_STATUS ||
            param->params->opcode == ESP_BLE_MESH_MODEL_OP_SENSOR_GET) {

            struct net_buf_simple *buf = param->status_cb.sensor_status.marshalled_sensor_data;
            if (buf && buf->len > 0) {
                ESP_LOGI(TAG, "Sensor data from 0x%04x, len=%d",
                         param->params->ctx.addr, buf->len);
                parse_sensor_status(buf->data, buf->len, param->params->ctx.addr);
            }
        }
    }
}

// ==================== POLL TASK ====================
static void sensor_poll_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(15000));

    while (1) {
        for (int i = 0; i < provisioned_count; i++) {
            if (provisioned_addr[i] == 0) continue;

            esp_ble_mesh_client_common_param_t common = {0};
            common.opcode = ESP_BLE_MESH_MODEL_OP_SENSOR_GET;
            common.model = &root_models[2];  // Sensor Client model
            common.ctx.net_idx = net_key_idx;
            common.ctx.app_idx = app_key_idx;
            common.ctx.addr = provisioned_addr[i];
            common.ctx.send_ttl = 7;
            common.msg_timeout = 5000;

            esp_ble_mesh_sensor_client_get_state_t get = {0};
            get.sensor_get.op_en = false;

            esp_err_t err = esp_ble_mesh_sensor_client_get_state(&common, &get);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "GET sent to node 0x%04x", provisioned_addr[i]);
            } else {
                ESP_LOGW(TAG, "GET failed to node 0x%04x: %d", provisioned_addr[i], err);
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ==================== BLE MESH INIT ====================
static esp_err_t ble_mesh_init(void)
{
    esp_ble_mesh_register_prov_callback(mesh_prov_cb);
    esp_ble_mesh_register_config_client_callback(mesh_config_client_cb);
    esp_ble_mesh_register_config_server_callback(mesh_config_server_cb);
    esp_ble_mesh_register_sensor_client_callback(mesh_sensor_client_cb);

    provision.prov_start_address = 0x0005;

    ESP_ERROR_CHECK(esp_ble_mesh_init(&provision, &composition));

    esp_ble_mesh_provisioner_set_dev_uuid_match(NULL, 0, 0, false);

    ESP_ERROR_CHECK(esp_ble_mesh_provisioner_prov_enable(
        ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));

    const uint8_t net_key[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                                  0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    const uint8_t app_key[16] = {0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                                  0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

    esp_ble_mesh_provisioner_add_local_net_key(net_key, net_key_idx);
    esp_ble_mesh_provisioner_add_local_app_key(app_key, net_key_idx, app_key_idx);
    //Bind AppKey cho Sensor Client model trên Gateway
    esp_ble_mesh_provisioner_bind_app_key_to_local_model(0x0001, app_key_idx,
        ESP_BLE_MESH_MODEL_ID_SENSOR_CLI, ESP_BLE_MESH_CID_NVAL);

    ESP_LOGI(TAG, "Gateway initialized, scanning for nodes...");
    return ESP_OK;
}

// ==================== MAIN ====================
void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "=== BLE Mesh Gateway Starting ===");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_init();
    mqtt_init();
    vTaskDelay(pdMS_TO_TICKS(2000));

    err = bluetooth_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "BT init failed"); return; }

    ble_mesh_get_dev_uuid(dev_uuid);
    err = ble_mesh_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Mesh init failed"); return; }

    xTaskCreate(sensor_poll_task, "sensor_poll", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== Gateway READY! ===");
}