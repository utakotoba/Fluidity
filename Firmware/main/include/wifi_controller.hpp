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

namespace wifi_controller {
class Controller {
 public:
  static Controller& get_instance() {
    static Controller instance;
    return instance;
  }

  esp_err_t init_sta();

  std::shared_ptr<std::string> get_ip_address();

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
  size_t retry_num_{0};

  // Event handlers
  void handle_event(esp_event_base_t event_base,
                    int32_t event_id,
                    void* event_data);

  static void wifi_event_handler(void* arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void* event_data) {
    static_cast<Controller*>(arg)->handle_event(event_base, event_id,
                                                event_data);
  }
};

esp_err_t launch();

std::string get_ip_address();
}  // namespace wifi_controller