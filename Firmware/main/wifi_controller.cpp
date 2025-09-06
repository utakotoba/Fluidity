#include "wifi_controller.hpp"
#include "esp_log.h"
#include <cstring>

namespace wifi_controller {

/**
 * @brief Convert ConnectionError enum to human-readable string
 * @param error The connection error to convert
 * @return String representation of the error
 */
const char* connection_error_to_string(ConnectionError error) {
  switch (error) {
    case ConnectionError::NONE:
      return "No error";
    case ConnectionError::INVALID_CREDENTIALS:
      return "Invalid credentials (SSID or password is empty/invalid)";
    case ConnectionError::SSID_NOT_FOUND:
      return "SSID not found in scan";
    case ConnectionError::AUTHENTICATION_FAILED:
      return "Authentication failed (wrong password)";
    case ConnectionError::CONNECTION_TIMEOUT:
      return "Connection timeout";
    case ConnectionError::UNKNOWN_ERROR:
      return "Unknown connection error";
    default:
      return "Undefined error";
  }
}

Controller::~Controller() {
  if (wifi_event_group_) {
    ESP_LOGI(TAG, "Deleting Wi-Fi event group...");
    vEventGroupDelete(wifi_event_group_);
    ESP_LOGI(TAG, "Wi-Fi event group is successfully deleted");
  }
}

bool Controller::validate_credentials(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) {
    ESP_LOGE(TAG, "WiFi SSID is empty");
    set_connection_error(ConnectionError::INVALID_CREDENTIALS);
    return false;
  }

  if (password.empty()) {
    ESP_LOGE(TAG, "WiFi password is empty");
    set_connection_error(ConnectionError::INVALID_CREDENTIALS);
    return false;
  }

  return true;
}

void Controller::set_connection_error(ConnectionError error) {
  last_error_.store(error, std::memory_order_release);
  connection_state_.store(ConnectionState::FAILED, std::memory_order_release);

  ESP_LOGE(TAG, "Connection error occurred: %s", connection_error_to_string(error));
}

void Controller::handle_wifi_event(int32_t event_id, void* event_data) {
  switch (event_id) {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "WiFi station started, attempting connection...");
      connection_state_.store(ConnectionState::CONNECTING,
                              std::memory_order_release);
      ESP_ERROR_CHECK(esp_wifi_connect());
      break;

    case WIFI_EVENT_STA_DISCONNECTED: {
      wifi_event_sta_disconnected_t* disconnected =
          static_cast<wifi_event_sta_disconnected_t*>(event_data);

      ESP_LOGW(TAG, "WiFi disconnected. Reason: %d", disconnected->reason);

      ConnectionError mapped_error = ConnectionError::UNKNOWN_ERROR;
      switch (disconnected->reason) {
        case WIFI_REASON_NO_AP_FOUND:
          mapped_error = ConnectionError::SSID_NOT_FOUND;
          break;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_EXPIRE:
          mapped_error = ConnectionError::AUTHENTICATION_FAILED;
          break;
        default:
          mapped_error = ConnectionError::UNKNOWN_ERROR;
          break;
      }
      
      set_connection_error(mapped_error);

      if (retry_num_ < WIFI_MAXIMUM_RETRY) {
        ESP_LOGI(TAG, "Retrying to connect to the AP... (attempt %zu/%zu)",
                 retry_num_ + 1, WIFI_MAXIMUM_RETRY);
        ESP_ERROR_CHECK(esp_wifi_connect());
        retry_num_++;
      } else {
        xEventGroupSetBits(wifi_event_group_, WIFI_FAIL_BIT);
        ESP_LOGE(TAG, "Failed to connect to the AP after %zu attempts",
                 WIFI_MAXIMUM_RETRY);
      }
      break;
    }

    default:
      break;
  }
}

void Controller::handle_ip_event(int32_t event_id, void* event_data) {
  if (event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&event->ip_info.ip));

    ip_address_.store(std::make_shared<std::string>(ip_addr),
                      std::memory_order_release);

    // Reset retry counter and update state
    retry_num_ = 0;
    last_error_.store(ConnectionError::NONE, std::memory_order_release);
    connection_state_.store(ConnectionState::CONNECTED,
                            std::memory_order_release);

    xEventGroupSetBits(wifi_event_group_, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Connected successfully! IP address: %s", ip_addr);
  }
}

esp_err_t Controller::setup_netif_and_events() {
  // Initialize NVS and create event group
  ESP_ERROR_CHECK(nvs_factory::ensure_initialized());
  wifi_event_group_ = xEventGroupCreate();
  ESP_LOGI(TAG, "Wi-Fi event group created");

  // Initialize network interface and event loop
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  ESP_LOGI(TAG, "Network interface setup completed");

  // Initialize WiFi with default configuration
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  // Register separate event handlers for WiFi and IP events
  esp_event_handler_instance_t wifi_instance;
  esp_event_handler_instance_t ip_instance;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, &wifi_instance));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, this, &ip_instance));

  ESP_LOGI(TAG, "Event handlers registered successfully");
  return ESP_OK;
}

esp_err_t Controller::configure_wifi_credentials(const std::string& ssid, const std::string& password) {
  // Validate credentials before attempting connection
  if (!validate_credentials(ssid, password)) {
    return ESP_ERR_INVALID_ARG;
  }

  // Configure WiFi credentials
  wifi_config_t wifi_config = {};
  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid.c_str(),
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password.c_str(),
          sizeof(wifi_config.sta.password) - 1);

  // Set station mode and apply configuration
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  ESP_LOGI(TAG, "WiFi credentials configured for SSID: %s", ssid.c_str());
  return ESP_OK;
}

esp_err_t Controller::start_wifi_and_wait() {
  // Start WiFi and wait for connection result
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "WiFi started, waiting for connection...");

  // Wait for connection or failure
  EventBits_t bits =
      xEventGroupWaitBits(wifi_event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Successfully connected to AP");
    return ESP_OK;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(TAG, "Failed to connect to AP");
    return ESP_FAIL;
  }

  ESP_LOGE(TAG, "Unexpected event bits received: 0x%x", bits);
  return ESP_ERR_INVALID_STATE;
}

esp_err_t Controller::init_sta(const std::string& ssid, const std::string& password) {
  // Reset state
  connection_state_.store(ConnectionState::DISCONNECTED,
                          std::memory_order_release);
  last_error_.store(ConnectionError::NONE, std::memory_order_release);
  retry_num_ = 0;

  // Setup network interface and event handlers
  esp_err_t ret = setup_netif_and_events();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to setup network interface and events");
    return ret;
  }

  // Configure WiFi credentials
  ret = configure_wifi_credentials(ssid, password);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure WiFi credentials");
    return ret;
  }

  // Start WiFi and wait for connection
  return start_wifi_and_wait();
}

std::shared_ptr<std::string> Controller::get_ip_address() {
  return ip_address_.load(std::memory_order_acquire);
}

ConnectionState Controller::get_connection_state() const {
  return connection_state_.load(std::memory_order_acquire);
}

ConnectionError Controller::get_last_error() const {
  return last_error_.load(std::memory_order_acquire);
}

esp_err_t launch(const std::string& ssid, const std::string& password) {
  return Controller::get_instance().init_sta(ssid, password);
}

std::string get_ip_address() {
  auto ip_addr_ptr = Controller::get_instance().get_ip_address();
  return ip_addr_ptr ? *ip_addr_ptr : std::string{};
}

ConnectionState get_connection_state() {
  return Controller::get_instance().get_connection_state();
}

ConnectionError get_last_error() {
  return Controller::get_instance().get_last_error();
}

}  // namespace wifi_controller