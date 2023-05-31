#pragma once

#include <Arduino.h>

#include "esp_https_ota.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"

#include <string.h>

static const char *HTTP_OTA_TAG = "HTTP OTA";


static inline bool needUpdate(esp_app_desc_t *new_app_info) {
  if (new_app_info == NULL) {
    return false;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;
  esp_ota_get_partition_description(running, &running_app_info);

  // Compare the app versions: if equal no update is needed
  if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
    return false;
  }

  return true;
}

void checkForOTA(
    const char *firmware_upgrade_url,
    int ota_recv_timeout,
    const char *ota_server_pem_start,
    bool skip_cert_common_name_check) {
  esp_http_client_config_t config;
  // Initialize config with null values to prevent undefined behaviour!
  memset(&config, 0, sizeof(config));

  config.url = firmware_upgrade_url,
  config.cert_pem = ota_server_pem_start,
  config.use_global_ca_store = true;
  config.timeout_ms = ota_recv_timeout,
  config.skip_cert_common_name_check = skip_cert_common_name_check;
  config.buffer_size = 128;
  config.buffer_size_tx = 128;

  esp_https_ota_config_t ota_config;
  // Initialize config with null values to prevent undefined behaviour!
  memset(&ota_config, 0, sizeof(ota_config));
  ota_config.http_config = &config;

  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(HTTP_OTA_TAG, "ESP HTTPS OTA Begin failed");
    return;
  }

  esp_app_desc_t app_desc;
  err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
  if (err != ESP_OK) {
    ESP_LOGE(HTTP_OTA_TAG, "esp_https_ota_read_img_desc failed");
  } else {
    if (!needUpdate(&app_desc)) {
      err = ESP_FAIL;
      ESP_LOGI(HTTP_OTA_TAG, "ESP_HTTPS_OTA no update needed!");
    } else {
      ESP_LOGI(HTTP_OTA_TAG, "Starting OTA Update!");

      while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
          break;
        }
      }

      if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(HTTP_OTA_TAG, "Complete data was not received.");
      }
    }
  }

  esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
  if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
    ESP_LOGI(HTTP_OTA_TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
    esp_restart();
  } else {
    if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGE(HTTP_OTA_TAG, "Image validation failed, image is corrupted");
    }
    ESP_LOGE(HTTP_OTA_TAG, "ESP_HTTPS_OTA upgrade failed!");
  }
}