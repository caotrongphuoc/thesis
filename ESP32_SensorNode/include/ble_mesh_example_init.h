#ifndef _BLE_MESH_EXAMPLE_INIT_H_
#define _BLE_MESH_EXAMPLE_INIT_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bluetooth_init(void);
void ble_mesh_get_dev_uuid(uint8_t *dev_uuid);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_MESH_EXAMPLE_INIT_H_ */