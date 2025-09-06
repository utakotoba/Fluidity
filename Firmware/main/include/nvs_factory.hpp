#pragma once

#include <atomic>
#include <mutex>
#include "esp_err.h"

/**
 * @file nvs_factory.hpp
 * @brief Thread-safe NVS (Non-Volatile Storage) factory for ESP-IDF
 * applications
 */

namespace nvs_factory {

/**
 * @brief Thread-safe singleton factory for NVS flash management
 */
class Factory {
 public:
  /**
   * @brief Get the singleton instance of the factory
   * @return Reference to the singleton Factory instance
   */
  static Factory& get_instance();

  /**
   * @brief Initialize NVS flash
   * @return ESP_OK on success, or an ESP error code on failure
   * @warning If initialization fails, subsequent NVS operations will fail
   */
  esp_err_t initialize();

  /**
   * @brief Deinitialize NVS flash
   * @return ESP_OK on success, or an ESP error code on failure
   * @warning After calling this, NVS operations will fail until reinitialized
   */
  esp_err_t deinitialize();

  /**
   * @brief Get the result of the last initialization attempt
   * @return ESP_ERR_INVALID_STATE if never initialized, otherwise the result
   *         of the initialization attempt
   */
  esp_err_t get_init_result();

  /**
   * @brief Check if NVS is currently initialized and ready for use
   * @return true if NVS is initialized and not deinitialized, false otherwise
   */
  bool is_initialized();

  // Prevent copying and assignment
  Factory(const Factory&) = delete;
  Factory& operator=(const Factory&) = delete;

 private:
  Factory() = default;
  ~Factory() = default;

  static constexpr const char* TAG = "fluidity_nvs_factory";

  mutable std::once_flag init_flag_;
  mutable std::once_flag deinit_flag_;
  std::atomic<esp_err_t> init_result_{ESP_ERR_INVALID_STATE};
  std::atomic<bool> deinitialized_{false};
};

/**
 * @brief Helper function to ensure NVS is initialized
 * @return ESP_OK on success, or an ESP error code on failure
 */
esp_err_t ensure_initialized();

}  // namespace nvs_factory