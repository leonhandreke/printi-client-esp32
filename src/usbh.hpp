/*
 * MIT License
 *
 * Copyright (c) 2021 touchgadgetdev@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <usb/usb_host.h>

const TickType_t HOST_EVENT_TIMEOUT = 1;
const TickType_t CLIENT_EVENT_TIMEOUT = 1;

typedef void (*usb_host_new_device_cb_t)(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl);
typedef void (*usb_host_device_gone_cb_t)(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl);

typedef struct {
  usb_host_client_handle_t client_hdl;
  uint8_t dev_addr;
  usb_device_handle_t dev_hdl;
  usb_host_new_device_cb_t new_device_cb;
  usb_host_device_gone_cb_t device_gone_cb;
} class_driver_t;

void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
  class_driver_t *driver_obj = (class_driver_t *)arg;

  esp_err_t err;
  switch (event_msg->event)
  {
    /**< A new device has been enumerated and added to the USB Host Library */
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      // If we're already attached to a device, just ignore
      if (driver_obj->dev_addr != 0) {
        ESP_LOGI("", "Ignoring new device event, already got device with address: %x", driver_obj->dev_addr);
      }

      driver_obj->dev_addr = event_msg->new_dev.address;
      ESP_LOGI("", "New device address: %d", event_msg->new_dev.address);

      err = usb_host_device_open(driver_obj->client_hdl, event_msg->new_dev.address, &driver_obj->dev_hdl);
      if (err != ESP_OK) ESP_LOGI("", "usb_host_device_open: %x", err);

      usb_device_info_t dev_info;
      err = usb_host_device_info(driver_obj->dev_hdl, &dev_info);
      if (err != ESP_OK) ESP_LOGI("", "usb_host_device_info: %x", err);
      ESP_LOGI("", "speed: %d dev_addr %d vMaxPacketSize0 %d bConfigurationValue %d",
          dev_info.speed, dev_info.dev_addr, dev_info.bMaxPacketSize0,
          dev_info.bConfigurationValue);

      const usb_device_desc_t *dev_desc;
      err = usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc);
      if (err != ESP_OK) ESP_LOGI("", "usb_host_get_device_descriptor: %x", err);
      usb_print_device_descriptor(dev_desc);

      const usb_config_desc_t *config_desc;
      err = usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc);
      if (err != ESP_OK) ESP_LOGI("", "usb_host_get_config_descriptor: %x", err);
      //usb_print_config_descriptor(config_desc, NULL);

      driver_obj->new_device_cb(driver_obj->client_hdl, driver_obj->dev_hdl);

      break;
    /**< A device opened by the client is now gone */
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      if (event_msg->dev_gone.dev_hdl != driver_obj->dev_hdl) {
        // We don't care about devices not managed by us
        return;
      }
      ESP_LOGI("", "Device Gone handle: %x", event_msg->dev_gone.dev_hdl);
      driver_obj->device_gone_cb(driver_obj->client_hdl, driver_obj->dev_hdl);
      ESP_LOGI("", "Device Gone callback executed: %x", event_msg->dev_gone.dev_hdl);
      ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
      driver_obj->dev_hdl = NULL;
      driver_obj->dev_addr = 0;
      break;
    default:
      ESP_LOGI("", "Unknown value %d", event_msg->event);
      break;
  }
}

// Reference: esp-idf/examples/peripherals/usb/host/usb_host_lib/main/usb_host_lib_main.c

class_driver_t* usbh_setup(usb_host_new_device_cb_t new_device_cb, usb_host_device_gone_cb_t device_gone_cb) {
  class_driver_t *driver_obj = (class_driver_t *) malloc(sizeof(class_driver_t));
  // Initialize dev_addr because that's how we know we're not yet attached to a device;
  driver_obj->dev_addr = 0;

  const usb_host_config_t config = {
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  esp_err_t err = usb_host_install(&config);
  ESP_LOGI("", "usb_host_install: %x", err);

  const usb_host_client_config_t client_config = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
        .client_event_callback = _client_event_callback,
        .callback_arg = driver_obj
    }
  };
  err = usb_host_client_register(&client_config, &driver_obj->client_hdl);
  ESP_LOGI("", "usb_host_client_register: %x", err);

  driver_obj->new_device_cb = new_device_cb;
  driver_obj->device_gone_cb = device_gone_cb;

  return driver_obj;
}

void usbh_handle(class_driver_t *driver_obj)
{
  uint32_t event_flags;
  static bool all_clients_gone = false;
  static bool all_dev_free = false;

  esp_err_t err = usb_host_lib_handle_events(HOST_EVENT_TIMEOUT, &event_flags);
  if (err == ESP_OK) {
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_LOGI("", "No more clients");
      all_clients_gone = true;
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI("", "No more devices");
      all_dev_free = true;
    }
  } else {
    if (err != ESP_ERR_TIMEOUT) {
      ESP_LOGI("", "usb_host_lib_handle_events: %x flags: %x", err, event_flags);
    }
  }

  err = usb_host_client_handle_events(driver_obj->client_hdl, CLIENT_EVENT_TIMEOUT);
  if ((err != ESP_OK) && (err != ESP_ERR_TIMEOUT)) {
    ESP_LOGI("", "usb_host_client_handle_events: %x", err);
  }
}

void usbh_task(void *arg) {
  void** args = (void**) arg;
  usb_host_new_device_cb_t new_device_cb = (usb_host_new_device_cb_t) args[0];
  usb_host_device_gone_cb_t device_gone_cb = (usb_host_device_gone_cb_t) args[1];
  class_driver_t *driver_obj = usbh_setup(new_device_cb, device_gone_cb);

  while (true) {
    usbh_handle(driver_obj);
  }

  vTaskSuspend(NULL);
}