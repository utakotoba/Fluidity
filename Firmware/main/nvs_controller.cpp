#include "nvs_controller.hpp"

namespace controller {

NvsController& NvsController::get_instance() {
  static NvsController instance;
  return instance;
}

esp_err_t NvsController::init() {
  std::lock_guard<std::mutex> lock(mutex_);

  ESP_LOGI(TAG, "NVS flash initializing...");

  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition was truncated");
    ESP_LOGI(TAG, "Erasing...");
    ret = nvs_flash_erase();
    if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "NVS partition not found");
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Reinitializing...");
    ret = nvs_flash_init();
  }

  state_.store(ret, std::memory_order_release);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "NVS initialized successfully");
  } else {
    ESP_LOGE(TAG, "Failed to initialize: %s", esp_err_to_name(ret));
  }

  return state_.load(std::memory_order_acquire);
}

esp_err_t NvsController::deinit() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_.load(std::memory_order_acquire) != ESP_OK) {
    return ESP_OK;
  }

  esp_err_t ret = nvs_flash_deinit();

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "NVS deinitialized succesfully");
    state_.store(ESP_ERR_INVALID_STATE, std::memory_order_release);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }
}

esp_err_t NvsController::get_current_state() {
  return state_.load(std::memory_order_acquire);
}

bool NvsController::is_ok() {
  return get_current_state() == ESP_OK;
}

NvsController::~NvsController() {
  deinit();
}

esp_err_t ensure_nvs() {
  return NvsController::get_instance().init();
}

}  // namespace controller