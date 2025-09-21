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

#include <string.h>
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

#include <esp_tls.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ESC_POS_Printer/ESC_POS_Printer.h"

#include "usbh.hpp"
#include "string_helper.h"

#include "Printer.hpp"
#include "ota.hpp"

static const char *TAG = "main";

extern const uint8_t logo_h58_start[] asm("_binary_resources_logo_h58_start");
extern const uint8_t logo_h58_end[] asm("_binary_resources_logo_h58_end");

extern const uint8_t config_html_start[] asm("_binary_resources_config_html_start");
extern const uint8_t config_html_end[] asm("_binary_resources_config_html_end");

extern const uint8_t letsencrypt_pem_start[] asm("_binary_resources_letsencrypt_pem_start");
extern const uint8_t letsencrypt_pem_end[] asm("_binary_resources_letsencrypt_pem_end");

extern const uint8_t logo_svg_start[] asm("_binary_resources_logo_svg_start");
extern const uint8_t logo_svg_end[] asm("_binary_resources_logo_svg_end");

extern const uint8_t courgette_ttf_start[] asm("_binary_resources_courgette_ttf_start");
extern const uint8_t courgette_ttf_end[] asm("_binary_resources_courgette_ttf_end");

String PRINTI_API_SERVER_BASE_URL = "https://api.printi.me";

const char *PREFERENCES_KEY_PRINTI_NAME = "printiName";
const char *PREFERENCES_KEY_WIFI_SSID = "wifiSsid";
const char *PREFERENCES_KEY_WIFI_PASSKEY = "wifiPasskey";
// Used to print a success message the first time we connect to a WiFi network
// Key is abbreviated because otherwise it will crash with TOO_LONG
const char *PREFERENCES_KEY_WIFI_PREVIOUSLY_CONNECTED = "prevConnected";

const char *CONFIG_MODE_AP_SSID = "printi";
const char *CONFIG_MODE_AP_PASSKEY = "12345678";

Preferences preferences;

WiFiClientSecure wifiClient;
HTTPClient http;

WebServer *server;

// TODO(Leon Handreke): USB handling is a fucking mess, there should not be three files that this is scattered over
// Needs much better separation of concerns!
uint8_t bInterfaceNumber;
uint8_t bInEndpointAddress;
uint8_t bOutEndpointAddress;

Printer *printer = NULL;
// ESC_POS_Printer is just a thin wrapper around Printer to implement some printer controll commands.
ESC_POS_Printer *esc_pos_printer = NULL;

bool otaUpdateInProgress = false;
bool configModeInProgress = false;

typedef enum {
  ORIGINAL_PRINTI,
  XIAMEN_BETTER_LITTLE_BLUE_CUTIE,
} printer_type_t;

printer_type_t printer_type = ORIGINAL_PRINTI;


void usb_new_device_cb(const usb_host_client_handle_t client_hdl, const usb_device_handle_t dev_hdl) {
  const usb_standard_desc_t *cur_desc;
  int cur_desc_offset;

  const usb_config_desc_t *config_desc;
  usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

  usb_intf_desc_t *printer_intf_desc = NULL;

  cur_desc = (const usb_standard_desc_t *) config_desc;
  cur_desc_offset = 0;
  while (printer_intf_desc == NULL) {
    cur_desc = usb_parse_next_descriptor_of_type(
      cur_desc, config_desc->wTotalLength, USB_B_DESCRIPTOR_TYPE_INTERFACE, &cur_desc_offset);

    if (cur_desc == NULL) {
      ESP_LOGI(TAG, "Interface Descriptor not found");
      return;
    }

    usb_intf_desc_t *intf_desc = (usb_intf_desc_t *) cur_desc;

    // USB Printer Class Specification 1.1
    if ((intf_desc->bInterfaceClass == USB_CLASS_PRINTER) && (intf_desc->bInterfaceSubClass == 1)) {
      printer_intf_desc = intf_desc;
      ESP_LOGI(TAG, "Found printer interface at: %x", intf_desc->bInterfaceNumber);
    }
  }

  ESP_LOGI(TAG, "Claiming interface: %x", printer_intf_desc->bInterfaceNumber);
  ESP_ERROR_CHECK(usb_host_interface_claim(client_hdl, dev_hdl,
    printer_intf_desc->bInterfaceNumber,
    printer_intf_desc->bAlternateSetting));


  const usb_ep_desc_t *in_ep_desc;
  const usb_ep_desc_t *out_ep_desc;
  // Find the printer outgoing endpoint
  for (int i = 0; i < printer_intf_desc->bNumEndpoints; i++) {
    // Strangely, usb_parse_endpoint_descriptor_by_index insists on giving back the offset of the endpoint descriptor
    int temp_offset = cur_desc_offset;
    const usb_ep_desc_t *ep_desc = usb_parse_endpoint_descriptor_by_index(printer_intf_desc,
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
    bInterfaceNumber = printer_intf_desc->bInterfaceNumber;
    bInEndpointAddress = in_ep_desc->bEndpointAddress;
    bOutEndpointAddress = out_ep_desc->bEndpointAddress;

    printer = new Printer(dev_hdl, in_ep_desc, out_ep_desc);
    esc_pos_printer = new ESC_POS_Printer(printer);
  }

  const usb_device_desc_t *dev_desc;
  ESP_ERROR_CHECK(usb_host_get_device_descriptor(dev_hdl, &dev_desc));
  if (dev_desc->idVendor == 0x28e9 && dev_desc->idProduct == 0x289) {
    ESP_LOGI(TAG, "Detected XIAMEN_BETTER_LITTLE_BLUE_CUTIE printer");
    printer_type = XIAMEN_BETTER_LITTLE_BLUE_CUTIE;
  }
}

void usb_device_gone_cb(const usb_host_client_handle_t client_hdl, const usb_device_handle_t dev_hdl) {
  delete printer;
  printer = nullptr;

  usb_host_endpoint_halt(dev_hdl, bInEndpointAddress);
  usb_host_endpoint_halt(dev_hdl, bOutEndpointAddress);
  usb_host_endpoint_flush(dev_hdl, bInEndpointAddress);
  usb_host_endpoint_flush(dev_hdl, bOutEndpointAddress);
  usb_host_interface_release(client_hdl, dev_hdl, bInterfaceNumber);
}

String getMacString() {
  char efuseStr[8];
  uint64_t efuseMac = ESP.getEfuseMac();
  sprintf(efuseStr, "%x%x%x%x",
          ((uint8_t *) (&efuseMac))[3],
          ((uint8_t *) (&efuseMac))[2],
          ((uint8_t *) (&efuseMac))[1],
          ((uint8_t *) (&efuseMac))[0]
  );
  return String(efuseStr);
}

String getPrintiName() {
  return preferences.getString(PREFERENCES_KEY_PRINTI_NAME);
}

void stopPrinter() {
  delete printer;
  printer = nullptr;
}

void _handleOtaUploadLoop(void *pvParameters) {
  while (true) {
    ArduinoOTA.handle();
    vTaskDelay(500);
  }
}

void startOtaUploadService() {
  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        stopPrinter();
        otaUpdateInProgress = true;

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        ESP_LOGI("OTA", "Start updating %s", type);
      })
      .onEnd([]() {
        otaUpdateInProgress = false;
        ESP_LOGI("OTA", "End");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        otaUpdateInProgress = true;
        ESP_LOGI("OTA", "Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        ESP_LOGE("OTA", "Error[%u]: ", error);
        otaUpdateInProgress = false;
        if (error == OTA_AUTH_ERROR)
          ESP_LOGE("OTA", "Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          ESP_LOGE("OTA", "Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          ESP_LOGE("OTA", "Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          ESP_LOGE("OTA", "Receive Failed");
        else if (error == OTA_END_ERROR)
          ESP_LOGE("OTA", "End Failed");
      });

  ArduinoOTA.setHostname(WiFi.getHostname());
  ArduinoOTA.setPassword("admin");
  ArduinoOTA.begin();

  xTaskCreate(
    _handleOtaUploadLoop, // Function that should be called
    "Handle OTA Upload", // Name of the task (for debugging)
    10000, // Stack size (bytes)
    NULL, // Parameter to pass
    10, // Task priority
    NULL // Task handle
  );
}

void _configServerLoop(void *pvParameters) {
  ESP_LOGI(TAG, "Starting config server task");

  bool printed_startup_message = false;

  while (true) {
    if (server == nullptr) {
      ESP_LOGI(TAG, "Server stopped, ending config server task");
      vTaskDelete(NULL);
      return;
    }
    server->handleClient();
    vTaskDelay(50);

    if (!printed_startup_message && printer != nullptr && esc_pos_printer != nullptr) {
      ESP_LOGI(TAG, "Print config server startup message");

      const char *image = (const char *) logo_h58_start;
      size_t image_len = logo_h58_end - logo_h58_start;
      printer->write((const uint8_t *) image, image_len);

      esc_pos_printer->println("");
      esc_pos_printer->println("=> Step 1:");
      esc_pos_printer->println("On your phone/laptop, connect to");
      esc_pos_printer->println("the WiFi network emitted by this");
      esc_pos_printer->println("printi:");
      esc_pos_printer->println("");
      esc_pos_printer->println(String("      Name: ") + CONFIG_MODE_AP_SSID);
      esc_pos_printer->println(String("  Password: ") + CONFIG_MODE_AP_PASSKEY);
      esc_pos_printer->println("");
      esc_pos_printer->println("=> Step 2:");
      esc_pos_printer->println("Once connected, open a web");
      esc_pos_printer->println("browser and navigate to:");
      esc_pos_printer->println("   http://192.168.4.1/");
      esc_pos_printer->println("");
      esc_pos_printer->println("=> Step 3:");
      esc_pos_printer->println("Give your printi a name and tell");
      esc_pos_printer->println("it about the WiFi network you");
      esc_pos_printer->println("want it to connect to");
      esc_pos_printer->println("");
      esc_pos_printer->println("That's it! Happy printing!");
      esc_pos_printer->println("");
      esc_pos_printer->println("");
      esc_pos_printer->println("");

      printed_startup_message = true;
    }
  }
}

void startConfigServer() {
  // If server already started, error out
  if (server != nullptr) {
    return;
  }

  // Will kill the main thread
  configModeInProgress = true;

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(CONFIG_MODE_AP_SSID, CONFIG_MODE_AP_PASSKEY);
  ESP_LOGI(TAG, "Started AP at IP %s", WiFi.softAPIP().toString().c_str());

  server = new WebServer(80);
  server->on("/", HTTP_GET, []() -> void {
    ESP_LOGI(TAG, "on /");

    size_t config_html_len = config_html_end - config_html_start;
    char *config_html = (char *) malloc(sizeof(char) * (config_html_len + 1));
    memcpy(config_html, config_html_start, config_html_len);
    config_html[config_html_len] = '\0';

    strreplace(config_html,
               "{{PRINTI_NAME}}",
               preferences.getString(PREFERENCES_KEY_PRINTI_NAME).c_str());
    strreplace(config_html,
               "{{PRINTI_NAME_TITLE}}",
               preferences.getString(PREFERENCES_KEY_PRINTI_NAME).c_str());
    strreplace(config_html,
               "{{SSID}}",
               preferences.getString(PREFERENCES_KEY_WIFI_SSID).c_str());
    strreplace(config_html,
               "{{PASSKEY}}",
               preferences.getString(PREFERENCES_KEY_WIFI_PASSKEY).c_str());

    server->send(200, "text/html", (const char *) config_html);
    free(config_html);
  });
  server->on("/", HTTP_POST, []() -> void {
    ESP_LOGI(TAG, "on POST /");
    preferences.putString(PREFERENCES_KEY_PRINTI_NAME, server->arg("printiName"));
    preferences.putString(PREFERENCES_KEY_WIFI_SSID, server->arg("ssid"));
    preferences.putString(PREFERENCES_KEY_WIFI_PASSKEY, server->arg("passkey"));

    preferences.putBool(PREFERENCES_KEY_WIFI_PREVIOUSLY_CONNECTED, false);

    server->send(200, "text/plain; charset=utf-8", "✅ Preferences saved, restarting...");
    //    server->sendHeader("Location", "/", true);
    //    server->send(303 /* See Other */, "text/html", "");
    ESP.restart();
  });
  
  // Add a ping
  server->on("/ping", HTTP_GET, []() -> void {
    server->send(200, "text/plain", "pong");
  });
  
  server->on("/logo.svg", HTTP_GET, []() -> void {
    server->send_P(200, PSTR("image/svg+xml"), (const char *) logo_svg_start, logo_svg_end - logo_svg_start);
  });
  server->on("/courgette.ttf", HTTP_GET, []() -> void {
    server->send_P(200, PSTR("font/ttf"), (const char *) courgette_ttf_start, courgette_ttf_end - courgette_ttf_start);
  });
  
    

  server->begin();

  ESP_LOGI(TAG, "Creating config server task");
  xTaskCreate(
    _configServerLoop, // Function that should be called
    "Config server", // Name of the task (for debugging)
    10000, // Stack size (bytes)
    NULL, // Parameter to pass
    10, // Task priority
    NULL // Task handle
  );
}

void stopConfigServer() {
  server->stop();
  delete server;
}

void _handleButtonLoop(void *pvParameters) {
  while (true) {
    if (digitalRead(0) == LOW) {
      ESP_LOGI(TAG, "Button pressed, starting config server");
      startConfigServer();
    }
    vTaskDelay(500);
  }
}

void startButtonHandler() {
  xTaskCreate(
    _handleButtonLoop, // Function that should be called
    "Handle Button", // Name of the task (for debugging)
    5000, // Stack size (bytes)
    NULL, // Parameter to pass
    10, // Task priority
    NULL // Task handle
  );
}

void setup() {
  esp_log_level_set("*", ESP_LOG_VERBOSE);

  // Use the line below on ESP32-S2 where the default serial port isn't wired up to a USB-Serial
  //Serial.begin(115200, SERIAL_8N1, 33, 34);
  // This is enough for the ESP32-S3 DevKit
  Serial.begin(115200);

  Serial.setDebugOutput(true);
  Serial.println("Gumo powerup");

  preferences.begin("printi");

  startButtonHandler();

  TaskHandle_t usb_host_driver_task_hdl;
  void *params[] = {(void *) usb_new_device_cb, (void *) usb_device_gone_cb};
  xTaskCreate(usbh_task,
              "usb_host_driver",
              4096,
              (void *) params,
              0,
              &usb_host_driver_task_hdl);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  // Set hostname so that they're easier to identify in the dashboard
  // Apparently required to get setHostname to work due to a bug
  //WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  //WiFi.config(((u32_t)0x0UL),((u32_t)0x0UL),((u32_t)0x0UL));
  String hostname = "printi";
  if (getPrintiName() != "") {
    hostname = hostname + "-" + getPrintiName().c_str();
  }
  WiFi.setHostname(hostname.c_str());
  // Redo WiFi config every time, costs a bit of startup time but avoids locking to one BSSID
  WiFi.persistent(false);

  String wifiSsid = preferences.getString(PREFERENCES_KEY_WIFI_SSID, "");
  String wifiPasskey = preferences.getString(PREFERENCES_KEY_WIFI_PASSKEY, "");

  if (wifiSsid == "") {
    ESP_LOGI(TAG, "Stored WiFi SSID is empty, starting config server");
    startConfigServer();
  } else {
    ESP_LOGI(TAG, "WiFi begin");
    WiFi.begin(wifiSsid.c_str(), wifiPasskey.c_str());

    for (int i = 5; i <= 5; i++) {
      if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected: %s BSSID %s", WiFi.localIP().toString().c_str(),
                 WiFi.BSSIDstr().c_str());
        break;
      }
    }
  }

  //startOtaUploadService();

  // TODO(Leon Handreke): Proper https
  wifiClient.setInsecure();
  esp_tls_init_global_ca_store();
  //const unsigned int letsencrypt_pem_len = ((char*) letsencrypt_pem_end) - ((char*) letsencrypt_pem_start);
  ESP_ERROR_CHECK(
    esp_tls_set_global_ca_store(letsencrypt_pem_start, letsencrypt_pem_end-letsencrypt_pem_start));
  //ESP_ERROR_CHECK(esp_tls_set_global_ca_store((const unsigned char*) LETSENCRYPT_CA_CERT, strlen(LETSENCRYPT_CA_CERT) + 1));

  checkForOTA("https://ndreke.de/~leon/dump/printi-firmware.bin", 5000, nullptr, true);
}

void printWifiConnectionInstructions() {
  ESP_LOGI(TAG, "Printing WiFi connection instructions");

  esc_pos_printer->println("This printi is not connected to the internet :(");
  esc_pos_printer->println("");
  esc_pos_printer->println("On the little printi brain board");
  esc_pos_printer->println("found on the bottom of your");
  esc_pos_printer->println("printi, press the button labeled");
  esc_pos_printer->println("\"0\" to enter configuration mode.");
  esc_pos_printer->println("");
}

void printPrintiServerErrorMessage() {
  ESP_LOGI(TAG, "Printing printi server error message");
  esc_pos_printer->println("Error: cannot reach printi server.");
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
bool printi_error_state_message_printed = true;

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
  if (otaUpdateInProgress || configModeInProgress) {
    ESP_LOGI(TAG, "OTA Update or config mode in progress, main thread suicide");
    vTaskDelete(NULL);
    return;
  }

  if (printer == nullptr) {
    vTaskDelay(500);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    set_printi_error_state(PRINTI_STATE_NO_WIFI);
    time_t error_state_duration = time(NULL) - printi_error_state_since;
    if (!printi_error_state_message_printed && error_state_duration > (10 * 60)) {
      printWifiConnectionInstructions();
      printi_error_state_message_printed = true;
    }
    WiFi.reconnect();
    return;
  }

  if (!canReach(PRINTI_API_SERVER_BASE_URL)) {
    set_printi_error_state(PRINTI_STATE_CANNOT_REACH_SERVER);
    time_t error_state_duration = time(NULL) - printi_error_state_since;
    if (!printi_error_state_message_printed && error_state_duration > (10 * 60)) {
      printPrintiServerErrorMessage();
      printi_error_state_message_printed = true;
    }
    return;
  }

  // We're PRINTI_STATE_HEALTHY!

  // If we were previously unhealthy
  if (printi_error_state != PRINTI_STATE_HEALTHY) {
    // Print the Connected message only if the error was previously printed.
    // If not, it was just a transient error that users don't have to know about.
    if (printi_error_state_message_printed) {
      ESP_LOGI(TAG, "Print Connected to printi.me message");
      esc_pos_printer->println("Connected! Go to: ");
      esc_pos_printer->print("  printi.me/");
      esc_pos_printer->println(getPrintiName());
    }

    set_printi_error_state(PRINTI_STATE_HEALTHY);
    printi_error_state_message_printed = true;
  }


  // Print welcome image
  if (!printed_startup_image) {
    const char *image = (const char *) logo_h58_start;
    size_t image_len = logo_h58_end - logo_h58_start;
    ESP_LOGI(TAG, "Print startup image");
    printer->write((const uint8_t *) image, image_len);
    //printWifiConnectionInstructions();

    printed_startup_image = true;
  }

  // Print a connected message the first time we connect to a WiFi network
  if (!preferences.getBool(PREFERENCES_KEY_WIFI_PREVIOUSLY_CONNECTED, false)) {
    preferences.putBool(PREFERENCES_KEY_WIFI_PREVIOUSLY_CONNECTED, true);

    esc_pos_printer->println("Connected lol! Go to: ");
    esc_pos_printer->print("  printi.me/");
    esc_pos_printer->println(getPrintiName());
    esc_pos_printer->println("");
    esc_pos_printer->println("");
    esc_pos_printer->println("");
  }

  String url = PRINTI_API_SERVER_BASE_URL + "/nextinqueue/" + getPrintiName();
  http.begin(wifiClient, url);
  wifiClient.setInsecure();
  http.setTimeout(40 * 1000);
  int response_code = http.GET();

  // USB cable may have been unplugged since we started the request
  if (printer == nullptr) {
    return;
  }

  if (response_code == 200) {
    String response = http.getString();
    ESP_LOGI(TAG, "reponse length %d", response.length());

    printer->write((const uint8_t *) response.c_str(), response.length());

    esc_pos_printer->println("");
    esc_pos_printer->println("");
    esc_pos_printer->println("");
    if (printer_type == XIAMEN_BETTER_LITTLE_BLUE_CUTIE) {
      esc_pos_printer->println("");
    }
  } else {
    ESP_LOGI(TAG, "HTTP response code: %x", response_code);
  }

  vTaskDelay(10);
}
