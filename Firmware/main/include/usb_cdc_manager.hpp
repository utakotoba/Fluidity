#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

/**
 * @file usb_cdc_manager.hpp
 * @brief USB CDC ACM device manager for handling serial device connections
 */

namespace usb_cdc_manager {

/**
 * @brief USB CDC ACM device manager class
 *
 * This class manages USB CDC ACM devices and provides a bridge between
 * telnet clients and USB serial devices.
 */
class UsbCdcManager {
 public:
  /**
   * @brief Constructor
   */
  UsbCdcManager();

  /**
   * @brief Destructor
   */
  ~UsbCdcManager();

  /**
   * @brief Initialize the USB CDC ACM manager
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t init();

  /**
   * @brief Deinitialize the USB CDC ACM manager
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t deinit();

  /**
   * @brief Check if manager is initialized
   * @return true if initialized, false otherwise
   */
  bool is_initialized() const;

  /**
   * @brief Open a CDC ACM device
   * @param vid Vendor ID
   * @param pid Product ID
   * @param instance Instance number (0 for single port devices)
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t open_device(uint16_t vid, uint16_t pid, uint8_t instance = 0);

  /**
   * @brief Close the currently open CDC ACM device
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t close_device();

  /**
   * @brief Check if a device is currently open
   * @return true if device is open, false otherwise
   */
  bool is_device_open() const;

  /**
   * @brief Send data to the CDC ACM device
   * @param data Data to send
   * @param len Length of data
   * @param timeout_ms Timeout in milliseconds
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t send_data(const uint8_t* data,
                      size_t len,
                      uint32_t timeout_ms = 1000);

  /**
   * @brief Set data received callback
   * @param callback Function to call when data is received
   */
  void set_data_callback(std::function<void(const uint8_t*, size_t)> callback);

  /**
   * @brief Get device information
   * @return Device information string
   */
  std::string get_device_info() const;

  /**
   * @brief Scan for available CDC ACM devices
   * @return String containing information about found devices
   */
  std::string scan_devices();

  /**
   * @brief List all USB devices connected to the system
   * @return String containing information about all connected USB devices
   */
  std::string list_all_usb_devices();

  // Prevent copying and assignment
  UsbCdcManager(const UsbCdcManager&) = delete;
  UsbCdcManager& operator=(const UsbCdcManager&) = delete;

 private:
  static constexpr const char* TAG = "usb_cdc_manager";
  static constexpr const uint32_t USB_HOST_PRIORITY = 20;
  static constexpr const size_t USB_LIB_TASK_STACK_SIZE = 4096;
  static constexpr const size_t BUFFER_SIZE = 512;

  std::atomic<bool> initialized_;
  std::atomic<bool> device_open_;
  cdc_acm_dev_hdl_t cdc_device_;
  TaskHandle_t usb_lib_task_handle_;
  SemaphoreHandle_t device_disconnected_sem_;
  std::function<void(const uint8_t*, size_t)> data_callback_;

  /**
   * @brief USB Host library handling task
   * @param arg Unused
   */
  static void usb_lib_task(void* arg);

  /**
   * @brief Data received callback
   * @param data Pointer to received data
   * @param data_len Length of received data in bytes
   * @param arg Argument passed to the device open function
   * @return true if data was processed, false if expecting more data
   */
  static bool handle_rx(const uint8_t* data, size_t data_len, void* arg);

  /**
   * @brief Device event callback
   * @param event Device event type and data
   * @param user_ctx Argument passed to the device open function
   */
  static void handle_event(const cdc_acm_host_dev_event_data_t* event,
                           void* user_ctx);
};

/**
 * @brief Get the singleton instance of the USB CDC manager
 * @return Reference to the singleton UsbCdcManager instance
 */
UsbCdcManager& get_instance();

/**
 * @brief Initialize the USB CDC manager
 * @return ESP_OK on success, error code on failure
 */
esp_err_t init();

/**
 * @brief Deinitialize the USB CDC manager
 * @return ESP_OK on success, error code on failure
 */
esp_err_t deinit();

/**
 * @brief Check if manager is initialized
 * @return true if initialized, false otherwise
 */
bool is_initialized();

/**
 * @brief Open a CDC ACM device
 * @param vid Vendor ID
 * @param pid Product ID
 * @param instance Instance number (0 for single port devices)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t open_device(uint16_t vid, uint16_t pid, uint8_t instance = 0);

/**
 * @brief Close the currently open CDC ACM device
 * @return ESP_OK on success, error code on failure
 */
esp_err_t close_device();

/**
 * @brief Check if a device is currently open
 * @return true if device is open, false otherwise
 */
bool is_device_open();

/**
 * @brief Send data to the CDC ACM device
 * @param data Data to send
 * @param len Length of data
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code on failure
 */
esp_err_t send_data(const uint8_t* data,
                    size_t len,
                    uint32_t timeout_ms = 1000);

/**
 * @brief Set data received callback
 * @param callback Function to call when data is received
 */
void set_data_callback(std::function<void(const uint8_t*, size_t)> callback);

/**
 * @brief Get device information
 * @return Device information string
 */
std::string get_device_info();

/**
 * @brief Scan for available CDC ACM devices
 * @return String containing information about found devices
 */
std::string scan_devices();

/**
 * @brief List all USB devices connected to the system
 * @return String containing information about all connected USB devices
 */
std::string list_all_usb_devices();

}  // namespace usb_cdc_manager
