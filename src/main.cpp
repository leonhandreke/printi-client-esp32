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

#include <Arduino.h>

#include "usbh.hpp"

extern const uint8_t logo_h58_start[] asm("_binary_resources_logo_h58_start");
extern const uint8_t logo_h58_end[] asm("_binary_resources_logo_h58_end");

const size_t PRINTER_OUT_BUFFERS = 300;
usb_transfer_t* printer_in = NULL;
usb_transfer_t* printer_out = NULL;

static void printer_transfer_cb(usb_transfer_t *transfer)
{
  ESP_LOGI("", "printer_transfer_cb context: %d", transfer->context);
  ESP_LOGI("", "printer_transfer_cb status %d", transfer->status);
}


void usb_new_device_cb(const usb_host_client_handle_t client_hdl, const usb_device_handle_t dev_hdl)
{
  const usb_standard_desc_t *cur_desc;
  int cur_desc_offset;

  const usb_config_desc_t *config_desc;
  usb_host_get_active_config_descriptor(dev_hdl, &config_desc);

  usb_intf_desc_t* printer_intf_desc = NULL;

  cur_desc = NULL;
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
    }
  }

  ESP_ERROR_CHECK(usb_host_interface_claim(client_hdl, dev_hdl,
                                           printer_intf_desc->bInterfaceNumber,
                                           printer_intf_desc->bAlternateSetting));


  // Find the printer outgoing endpoint
  for (int i = 0; i < printer_intf_desc->bNumEndpoints; i++) {

  }
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
        ESP_ERROR_CHECK(usb_host_transfer_alloc(ep_desc->wMaxPacketSize, 0, &printer_in));
        printer_in->device_handle = dev_hdl;
        printer_in->bEndpointAddress = ep_desc->bEndpointAddress;
        printer_in->callback = printer_transfer_cb;
        printer_in->context = NULL;
      } else {
        ESP_ERROR_CHECK(usb_host_transfer_alloc(
            ep_desc->wMaxPacketSize*PRINTER_OUT_BUFFERS,
            0, &printer_out));
        printer_out->device_handle = dev_hdl;
        printer_out->bEndpointAddress = ep_desc->bEndpointAddress;
        printer_out->callback = printer_transfer_cb;
        printer_out->context = NULL;
      }
    }
}

void setup()
{
    esp_log_level_set("*", ESP_LOG_DEBUG);

//  delay(1000);
//  pinMode(15, OUTPUT);
//  delay(500);
//  Serial.begin(115200, SERIAL_8N1, 33, 34);
//  Serial.setDebugOutput(true);
//  Serial.println("Gumo powerup");

   usbh_setup(usb_new_device_cb);

}

void loop()
{
  // TODO(Leon Handreke): Migrate this to a separate task?
  usbh_task();

  // ESP32 S2 Typewriter
  // Read line from serial monitor and write to printer.
//  String aLine = Serial.readStringUntil('\n');  // Read line ending with newline
//
//  if (aLine.length() > 0) {
//    // readStringUntil removes the newline so add it back
//    aLine.concat('\n');
//    PrinterOut->num_bytes = aLine.length();
//    memcpy(PrinterOut->data_buffer, aLine.c_str(), PrinterOut->num_bytes);
//    esp_err_t err = usb_host_transfer_submit(PrinterOut);
//    if (err != ESP_OK) {
//      ESP_LOGI("", "usb_host_transfer_submit Out fail: %x", err);
//    }
//  }

//  if (PrinterOut != NULL && !printed_image) {
//    printed_image = 1;
//
//    char* image = (char*) logo_h58_start;
//    int image_len = logo_h58_end - logo_h58_start;
//
//    ESP_LOGI("", "logo_h58 length %d", image_len);
//
//    while (image_len > 0) {
//      int tx_len = image_len > PrinterOut->data_buffer_size ? PrinterOut->data_buffer_size : image_len;
//
//      ESP_LOGI("", "transmit len %d", tx_len);
//
//      PrinterOut->num_bytes = tx_len;
//      memcpy(PrinterOut->data_buffer, image, tx_len);
//      image = image + tx_len;
//      image_len = image_len - tx_len;
//
//      esp_err_t err = usb_host_transfer_submit(PrinterOut);
//      if (err != ESP_OK) {
//        ESP_LOGI("", "usb_host_transfer_submit Out fail: %x", err);
//      }
//    }
//
//  }
}
