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

#define ARDUINO_USB_MODE 1
#define ARDUINO_USB_CDC_ON_BOOT 0

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ESC_POS_Printer/ESC_POS_Printer.h"

#include "usbh.hpp"

#include "Printer.hpp"

extern const uint8_t logo_h58_start[] asm("_binary_resources_logo_h58_start");
extern const uint8_t logo_h58_end[] asm("_binary_resources_logo_h58_end");

String PRINTI_API_SERVER_BASE_URL = "https://api.printi.me";

WiFiClientSecure wifiClient;
HTTPClient http;

Printer* printer = NULL;
ESC_POS_Printer* esc_pos_printer = NULL;


void usb_new_device_cb(const usb_host_client_handle_t client_hdl, const usb_device_handle_t dev_hdl)
{
  const usb_standard_desc_t *cur_desc;
  int cur_desc_offset;

  const usb_config_desc_t *config_desc;
  usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

  usb_intf_desc_t* printer_intf_desc = NULL;

  cur_desc = (const usb_standard_desc_t *) config_desc;
  cur_desc_offset = 0;
  while (printer_intf_desc == NULL) {
    cur_desc = usb_parse_next_descriptor_of_type(
        cur_desc, config_desc->wTotalLength, USB_B_DESCRIPTOR_TYPE_INTERFACE, &cur_desc_offset);

    if (cur_desc == NULL) {
      ESP_LOGI("", "Interface Descriptor not found");
      return;
    }

    usb_intf_desc_t *intf_desc = (usb_intf_desc_t *) cur_desc;

    // USB Printer Class Specification 1.1
    if ((intf_desc->bInterfaceClass == USB_CLASS_PRINTER) && (intf_desc->bInterfaceSubClass == 1)) {
      printer_intf_desc = intf_desc;
      ESP_LOGI("", "Found printer interface at: %x", intf_desc->bInterfaceNumber);
    }
  }

  ESP_LOGI("", "Claiming interface: %x", printer_intf_desc->bInterfaceNumber);
  ESP_ERROR_CHECK(usb_host_interface_claim(client_hdl, dev_hdl,
                                           printer_intf_desc->bInterfaceNumber,
                                           printer_intf_desc->bAlternateSetting));


  const usb_ep_desc_t *in_ep_desc;
  const usb_ep_desc_t *out_ep_desc;
  // Find the printer outgoing endpoint
  for (int i = 0; i < printer_intf_desc->bNumEndpoints; i++) {

    // Strangely, usb_parse_endpoint_descriptor_by_index insists on giving back the offset of the endpoint descriptor
    int temp_offset = cur_desc_offset;
    const usb_ep_desc_t* ep_desc = usb_parse_endpoint_descriptor_by_index(printer_intf_desc,
                                                                          i,
                                                                          config_desc->wTotalLength,
                                                                          &temp_offset);
    // Must be bulk transfer for printer
    if ((ep_desc->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK) {
      //  Bits 7 Direction 0 = Out, 1 = In (Ignored for Control Endpoints)
      if (USB_EP_DESC_GET_EP_DIR(ep_desc) == 1) {
        in_ep_desc = ep_desc;
      } else {
        out_ep_desc = ep_desc;
      }
    }
  }

  if (in_ep_desc != nullptr && out_ep_desc != nullptr) {
    printer = new Printer(dev_hdl, in_ep_desc, out_ep_desc);
    esc_pos_printer = new ESC_POS_Printer(printer);
  }
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

//  delay(1000);
//  pinMode(15, OUTPUT);
//  delay(500);
  Serial.begin(115200, SERIAL_8N1, 33, 34);
  Serial.setDebugOutput(true);
  Serial.println("Gumo powerup");

  TaskHandle_t usb_host_driver_task_hdl;
  xTaskCreate(usbh_task,
              "usb_host_driver",
              4096,
              (void *)usb_new_device_cb,
              0,
              &usb_host_driver_task_hdl);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  // Set hostname so that they're easier to identify in the dashboard
  // Apparently required to get setHostname to work due to a bug
  //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.config(((u32_t)0x0UL),((u32_t)0x0UL),((u32_t)0x0UL));
  WiFi.setHostname("printi");

//  WiFi.mode(WIFI_AP);
//  WiFi.softAP("esp32", NULL);
  WiFi.begin("virus89.exe-24ghz", "Mangoldsalat2019");

  for (int i = 5; i <= 5; i++) {
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      Serial.print("WiFi connected: ");
      Serial.println(WiFi.localIP());
      return;
    }
  }

  // TODO(Leon Handreke): Proper https
  wifiClient.setInsecure();
}

const char* getPrintiName() {
  return "mango";
}

void printWifiConnectionInstructions() {
  ESP_LOGI("", "Printing WiFi connection instructions");
}

void printPrintiServerErrorMessage() {
  ESP_LOGI("", "Printing printi server error message");
}

bool canReach(String url) {
  http.begin(wifiClient, url);
  wifiClient.setInsecure();
  http.setTimeout(10 * 1000);
  int response_code = http.GET();
  if (response_code == 200) {
    return true;
  } else {
    return false;
  }
}

typedef enum {
  PRINTI_STATE_NO_WIFI,
  PRINTI_STATE_CANNOT_REACH_SERVER,
  PRINTI_STATE_HEALTHY,
} printi_error_state_t;

printi_error_state_t printi_error_state = PRINTI_STATE_HEALTHY;
time_t printi_error_state_since;
bool printi_error_state_message_printed;

void set_printi_error_state(printi_error_state_t new_state) {
  if (printi_error_state == new_state) {
    return;
  }
  printi_error_state = new_state;
  printi_error_state_since = time(NULL);
  printi_error_state_message_printed = false;
}

bool printed_startup_image = false;

void loop() {
  if (printer == nullptr) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (printi_error_state == PRINTI_STATE_HEALTHY || printi_error_state == PRINTI_STATE_CANNOT_REACH_SERVER) {
      set_printi_error_state(PRINTI_STATE_NO_WIFI);
      time_t error_state_duration = time(NULL) - printi_error_state_since;
      if (!printi_error_state_message_printed && error_state_duration > (5*60)) {
        printWifiConnectionInstructions();
        printi_error_state_message_printed = true;
      }
      return;
    }
  }

  if (!canReach(PRINTI_API_SERVER_BASE_URL)) {
    if (printi_error_state == PRINTI_STATE_HEALTHY) {
      printi_error_state = PRINTI_STATE_CANNOT_REACH_SERVER;
      time_t error_state_duration = time(NULL) - printi_error_state_since;
      if (!printi_error_state_message_printed && error_state_duration > (5*60)) {
        printPrintiServerErrorMessage();
        printi_error_state_message_printed = true;
      }
      return;
    }
  }

  // We're transitioning to PRINTI_STATE_HEALTHY!
  if (printi_error_state != PRINTI_STATE_HEALTHY) {
    set_printi_error_state(PRINTI_STATE_HEALTHY);

    ESP_LOGI("", "Print Connected to printi.me message");
    //esc_pos_printer->println("Connected! Go to: ");
    //esc_pos_printer->print("  printi.me/");
    //esc_pos_printer->println(getPrintiName());

    printi_error_state_message_printed = true;

  }

  // Print welcome image
  if (!printed_startup_image) {
    const char *image = (const char *) logo_h58_start;
    size_t image_len = logo_h58_end - logo_h58_start;
    ESP_LOGI("", "Print startup image");
    //printer->write((const uint8_t *) image, image_len);
    printed_startup_image = true;
  }


  String url = PRINTI_API_SERVER_BASE_URL + "/nextinqueue/" + getPrintiName();
  http.begin(wifiClient, url);
  wifiClient.setInsecure();
  http.setTimeout(40 * 1000);
  int response_code = http.GET();

  if (response_code == 200) {
    String response = http.getString();
    ESP_LOGI("", "reponse length %d", response.length());
    printer->write((const uint8_t *)response.c_str(), response.length());
    esc_pos_printer->println("");
    esc_pos_printer->println("");
    esc_pos_printer->println("");
  } else {
    ESP_LOGI("", "HTTP response code: %x", response_code);
  }

  vTaskDelay(10);
}
