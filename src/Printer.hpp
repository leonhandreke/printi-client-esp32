#include <Arduino.h>

#include <usb/usb_host.h>

#include <FreeRTOS.h>
#include <freertos/semphr.h>

static const char* TAG = "Printer";

class Printer : public Print {
private:
  // Out buffer 90k
  size_t OUT_BUFFER_SIZE = 30 * 1024;
  SemaphoreHandle_t transfer_semaphore;

  usb_transfer_t* in_transfer;
  usb_transfer_t* out_transfer;

  static void _transfer_cb(usb_transfer_t *transfer)
  {
    Printer* printer = static_cast<Printer*>(transfer->context);
    printer->transfer_cb(transfer);
  }

  void transfer_cb(usb_transfer_t* transfer) {
    ESP_LOGI(TAG, "Release transfer semaphore");
    xSemaphoreGive(transfer_semaphore);

    ESP_LOGI("", "printer_transfer_cb context: %d", transfer->context);
    ESP_LOGI("", "printer_transfer_cb status %d", transfer->status);
  }

public:
  Printer(usb_device_handle_t dev_hdl, const usb_ep_desc_t* in_ep_desc, const usb_ep_desc_t* out_ep_desc) {
    ESP_ERROR_CHECK(usb_host_transfer_alloc(in_ep_desc->wMaxPacketSize, 0, &in_transfer));
    in_transfer->device_handle = dev_hdl;
    in_transfer->bEndpointAddress = in_ep_desc->bEndpointAddress;
    in_transfer->callback = _transfer_cb;
    in_transfer->context = this;
    ESP_LOGI("", "Allocated printer in transfer with data_buffer_size: %d", in_transfer->data_buffer_size);

    ESP_ERROR_CHECK(usb_host_transfer_alloc(
        OUT_BUFFER_SIZE,
        0, &out_transfer));
    out_transfer->device_handle = dev_hdl;
    out_transfer->bEndpointAddress = out_ep_desc->bEndpointAddress;
    out_transfer->callback = _transfer_cb;
    out_transfer->context = this;
    ESP_LOGI("", "Allocated printer out transfer with data_buffer_size: %d", out_transfer->data_buffer_size);

    transfer_semaphore = xSemaphoreCreateBinary();
    // Semaphore is initially empty and must be given before use
    xSemaphoreGive(transfer_semaphore);
  }

  size_t write (uint8_t x) {
    ESP_LOGI(TAG, "WRITE 1");

    return write(&x, 1);
  }

  size_t write(const uint8_t *buffer, size_t size) {
    ESP_LOGI(TAG, "WRITE BUF");

    if (out_transfer->data_buffer_size < size) {
      ESP_LOGE(TAG, "USB transfer buffer size %d too small for response length %d",
               out_transfer->data_buffer_size, size);
      return 0;
    }

    if (xSemaphoreTake(transfer_semaphore, (TickType_t) 1000) == pdTRUE) {
      ESP_LOGI(TAG, "Acquired semaphore for transfer");
      out_transfer->num_bytes = size;
      memcpy(out_transfer->data_buffer, buffer, size);
      ESP_LOGI(TAG, "Submit USB bulk transfer of size: %d", size);
      ESP_ERROR_CHECK(usb_host_transfer_submit(out_transfer));
    } else {
      ESP_LOGE(TAG, "Failed to acquire semaphore to transfer");
      return 0;
    }
    return size;
  }
};
