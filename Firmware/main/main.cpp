#include "esp_log.h"
#include "nvs_factory.hpp"
#include "wifi_controller.hpp"

static const char* TAG = "fluidity_main";

extern "C" void app_main() {
  nvs_factory::ensure_initialized();
  wifi_controller::launch();
  ESP_LOGI(TAG, "Connected with IP: %s",
           wifi_controller::get_ip_address().c_str());
}