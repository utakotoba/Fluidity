#include "nvs_factory.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

namespace nvs_factory {
Factory& Factory::get_instance() {
  static Factory instance;
  return instance;
}

esp_err_t Factory::initialize() {
  std::call_once(init_flag_, [this]() {
    ESP_LOGI(TAG, "Initialize NVS flash...");

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGW(TAG, "NVS partition was truncated");
      ESP_LOGI(TAG, "Erasing...");
      ESP_ERROR_CHECK(nvs_flash_erase());
      ESP_LOGI(TAG, "Reinitializing...");
      ret = nvs_flash_init();
    }

    init_result_.store(ret, std::memory_order_release);

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "NVS initialized successfully");
    } else {
      ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
    }
  });

  return init_result_.load(std::memory_order_acquire);
}

esp_err_t Factory::deinitialize() {
  if (init_result_.load(std::memory_order_acquire) != ESP_OK) {
    return ESP_OK;
  }

  esp_err_t ret = ESP_OK;

  std::call_once(deinit_flag_, [this, &ret]() {
    ret = nvs_flash_deinit();

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "NVS deinitialized successfully");
      deinitialized_.store(true, std::memory_order_release);
    } else {
      ESP_LOGE(TAG, "Failed to deinitialize NVS: %s", esp_err_to_name(ret));
    }
  });

  return ret;
}

esp_err_t Factory::get_init_result() {
  return init_result_.load(std::memory_order_acquire);
}

bool Factory::is_initialized() {
  return init_result_.load(std::memory_order_acquire) == ESP_OK &&
         !deinitialized_.load(std::memory_order_acquire);
}

esp_err_t ensure_initialized() {
  return Factory::get_instance().initialize();
}
}  // namespace nvs_factory