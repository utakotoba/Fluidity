#include "nvs_controller.hpp"
#include "sdkconfig.h"
#include "wifi_controller.hpp"

extern "C" void app_main() {
  controller::ensure_nvs();
  controller::wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
}