#pragma once

#include <atomic>
#include <mutex>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_controller.hpp"

namespace controller {

enum connection_state {
  DISCONNECTED,   // Not connected to any network
  CONNECTING,     // Attempt to connect a network
  CONNECTED,      // Successfully connected with IP
  FAILED,         // Connection failed after maximum retries
  UNINITIALIZED,  // STA is not initialized
};

enum connection_error {
  NONE,                   // No Error
  INVALID_CREDENTIALS,    // SSID or password is empty or invalid
  SSID_NOT_FOUND,         // SSID not found in nearby
  AUTHENTICATION_FAILED,  // Wrong password
  CONNECTION_TIMEOUT,     // Connection timeout
  UNKNOWN_ERROR,          // Unknown connection error
};

class WifiController {
 public:
  static WifiController& get_instance();

  esp_err_t init_sta();
  esp_err_t connect(const char* ssid, const char* password);
  esp_err_t disconnect();

  connection_state get_connection_state();
  connection_error get_last_error();

  WifiController(const WifiController&) = delete;
  WifiController& operator=(const WifiController&) = delete;

 private:
  WifiController() = default;
  ~WifiController();

  static constexpr const char* TAG = "wifi_controller";
  static constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
  static constexpr EventBits_t WIFI_FAIL_BIT = BIT1;

  mutable std::mutex mutex_;
  EventGroupHandle_t wifi_event_group_{nullptr};

  std::atomic<connection_state> state_{connection_state::UNINITIALIZED};
  std::atomic<connection_error> last_error_{connection_error::NONE};
  uint32_t retry_num_{0};

  std::atomic<char*> current_ssid_{nullptr};
  std::atomic<char*> ip_address_{nullptr};

  void set_connection_error(connection_error error);

  esp_err_t setup_netif_and_event_group();
  bool validate_credentials(const char* ssid, const char* password);
  void handle_wifi_event(int32_t event_id, void* event_data);
  void handle_ip_event(int32_t event_id, void* event_data);

  static void wifi_event_handler(void* arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void* event_data) {
    auto* controller = static_cast<WifiController*>(arg);
    controller->handle_wifi_event(event_id, event_data);
  };

  static void ip_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data) {
    auto* controller = static_cast<WifiController*>(arg);
    controller->handle_ip_event(event_id, event_data);
  };
};

esp_err_t wifi_connect(const char* ssid, const char* password);

}  // namespace controller