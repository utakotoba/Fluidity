#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_factory.hpp"
#include "sdkconfig.h"

/**
 * @file wifi_controller.hpp
 * @brief Thread-safe WiFi controller for ESP-IDF applications
 */

namespace wifi_controller {

/**
 * @brief Enumeration for WiFi connection states
 */
enum class ConnectionState {
  DISCONNECTED,  // Not connected to any network
  CONNECTING,    // Attempting to connect
  CONNECTED,     // Successfully connected with IP
  FAILED         // Connection failed after maximum retries
};

/**
 * @brief Enumeration for WiFi connection error types
 */
enum class ConnectionError {
  NONE,                   // No error
  INVALID_CREDENTIALS,    // SSID or password is empty/invalid
  SSID_NOT_FOUND,         // SSID not found in scan
  AUTHENTICATION_FAILED,  // Wrong password
  CONNECTION_TIMEOUT,     // Connection timeout
  UNKNOWN_ERROR           // Unknown connection error
};

/**
 * @brief Thread-safe singleton WiFi controller for ESP32
 *
 * This class manages WiFi station mode connections with automatic retry
 * logic and provides thread-safe access to connection status and IP address.
 */
class Controller {
 public:
  /**
   * @brief Get the singleton instance of the WiFi controller
   * @return Reference to the singleton Controller instance
   */
  static Controller& get_instance() {
    static Controller instance;
    return instance;
  }

  /**
   * @brief Initialize WiFi in station mode and attempt connection
   * @return ESP_OK on successful connection, ESP_ERR_INVALID_ARG if credentials
   *         are invalid, or other ESP error codes on failure
   * @warning This function blocks until connection succeeds or fails
   */
  esp_err_t init_sta();

  /**
   * @brief Get the current IP address as a shared pointer
   * @return Shared pointer to IP address string, or nullptr if not connected
   * @note Thread-safe operation
   */
  std::shared_ptr<std::string> get_ip_address();

  /**
   * @brief Get the current connection state
   * @return Current connection state
   * @note Thread-safe operation
   */
  ConnectionState get_connection_state() const;

  /**
   * @brief Get the last connection error
   * @return Last connection error type
   * @note Thread-safe operation
   */
  ConnectionError get_last_error() const;

  // Prevent copying and assignment
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

 private:
  Controller() = default;
  ~Controller();

  static constexpr const char* TAG = "fluidity_wifi_controller";

  // Wi-Fi configurations
  static constexpr const char* WIFI_SSID = CONFIG_WIFI_SSID;
  static constexpr const char* WIFI_PASSWORD = CONFIG_WIFI_PASSWORD;
  static constexpr const size_t WIFI_MAXIMUM_RETRY = 5;

  // Wi-Fi event bits
  static constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
  static constexpr EventBits_t WIFI_FAIL_BIT = BIT1;

  EventGroupHandle_t wifi_event_group_{nullptr};
  std::atomic<std::shared_ptr<std::string>> ip_address_{nullptr};
  std::atomic<ConnectionState> connection_state_{ConnectionState::DISCONNECTED};
  std::atomic<ConnectionError> last_error_{ConnectionError::NONE};
  size_t retry_num_{0};

  // Private helper methods
  esp_err_t setup_netif_and_events();
  esp_err_t configure_wifi_credentials();
  esp_err_t start_wifi_and_wait();
  bool validate_credentials();
  void set_connection_error(ConnectionError error);

  // Event handlers
  void handle_wifi_event(int32_t event_id, void* event_data);
  void handle_ip_event(int32_t event_id, void* event_data);

  // Event handler wrappers
  static void wifi_event_handler(void* arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void* event_data) {
    auto* controller = static_cast<Controller*>(arg);
    controller->handle_wifi_event(event_id, event_data);
  }

  static void ip_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data) {
    auto* controller = static_cast<Controller*>(arg);
    controller->handle_ip_event(event_id, event_data);
  }
};

/**
 * @brief Launch WiFi controller and attempt connection
 * @return ESP_OK on successful connection, or an ESP error code on failure
 * @note This is a convenience function that calls
 * Controller::get_instance().init_sta()
 */
esp_err_t launch();

/**
 * @brief Get the current IP address as a string
 * @return IP address string, or empty string if not connected
 * @note Thread-safe operation
 */
std::string get_ip_address();

/**
 * @brief Get the current connection state
 * @return Current connection state
 * @note Thread-safe operation
 */
ConnectionState get_connection_state();

/**
 * @brief Get the last connection error
 * @return Last connection error type
 * @note Thread-safe operation
 */
ConnectionError get_last_error();

}  // namespace wifi_controller