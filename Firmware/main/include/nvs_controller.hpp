#pragma once

#include <atomic>
#include <mutex>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

namespace controller {

class NvsController {
 public:
  static NvsController& get_instance();

  esp_err_t init();
  esp_err_t deinit();

  esp_err_t get_current_state();
  bool is_ok();

  NvsController(const NvsController&) = delete;
  NvsController& operator=(const NvsController&) = delete;

 private:
  NvsController() = default;
  ~NvsController();

  static constexpr const char* TAG = "nvs_controller";

  mutable std::mutex mutex_;
  std::atomic<esp_err_t> state_{ESP_ERR_INVALID_STATE};
};

esp_err_t ensure_nvs();

}  // namespace controller
