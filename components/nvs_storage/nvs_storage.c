/**
 * @file nvs_storage.c
 * @brief NVS implementation
 */

#include "nvs_storage.h"
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static esp_err_t nvs_open_handle(nvs_handle_t *out_handle) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        *out_handle = handle;
    }
    return err;
}

esp_err_t nvs_save_float(const char *key, float value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(&handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_blob(handle, key, &value, sizeof(value));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_float(const char *key, float *value, float default_val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(&handle);
    if (err != ESP_OK) {
        *value = default_val;
        return err;
    }
    
    size_t size = sizeof(float);
    err = nvs_get_blob(handle, key, value, &size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_val;
        return ESP_OK;  // Not an error, just first run
    }
    return err;
}

esp_err_t nvs_save_int(const char *key, int32_t value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(&handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_int(const char *key, int32_t *value, int32_t default_val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open_handle(&handle);
    if (err != ESP_OK) {
        *value = default_val;
        return err;
    }
    
    err = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_val;
        return ESP_OK;
    }
    return err;
}
