#include "nvs_controller.hpp"

extern "C" void app_main() {
  controller::ensure_nvs();
}