#include "nvs_factory.hpp"

// static const char* TAG = "fluidity_main";

extern "C" void app_main() {
  nvs_factory::ensure_initialized();
}