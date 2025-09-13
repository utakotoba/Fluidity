#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_factory.hpp"
#include "wifi_controller.hpp"
#include "telnet_server.hpp"
#include "usb_cdc_manager.hpp"

static const char* TAG = "fluidity_main";

extern "C" void app_main() {
  ESP_LOGI(TAG, "Starting Fluidity firmware...");

  esp_err_t nvs_ret = nvs_factory::ensure_initialized();
  if (nvs_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(nvs_ret));
    esp_deep_sleep_start();
    return;
  }

  esp_err_t wifi_ret = wifi_controller::launch(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
  if (wifi_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to launch WiFi controller: %s",
             esp_err_to_name(wifi_ret));
    esp_deep_sleep_start();
    return;
  }

  wifi_controller::ConnectionState state =
      wifi_controller::get_connection_state();
  if (state != wifi_controller::ConnectionState::CONNECTED) {
    ESP_LOGE(TAG, "WiFi connection failed with state: %d",
             static_cast<int>(state));
    esp_deep_sleep_start();
    return;
  }

  std::string ip_address = wifi_controller::get_ip_address();
  ESP_LOGI(TAG, "WiFi connection successful!");
  ESP_LOGI(TAG, "Connected with IP: %s", ip_address.c_str());

  // Start telnet server
  esp_err_t telnet_ret = telnet_server::launch(23);
  if (telnet_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start telnet server: %s", esp_err_to_name(telnet_ret));
    esp_deep_sleep_start();
    return;
  }

  ESP_LOGI(TAG, "Telnet server started on port 23");
  
  // Initialize USB CDC manager (lazy initialization - will be done when first serial command is used)
  ESP_LOGI(TAG, "USB CDC manager will be initialized on first use");
  
  ESP_LOGI(TAG, "System ready for operation");
  ESP_LOGI(TAG, "Connect with: telnet %s 23", ip_address.c_str());
  ESP_LOGI(TAG, "Use 'serial' command to connect to USB CDC ACM devices");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}