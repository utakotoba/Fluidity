#include "wifi_controller.hpp"

namespace controller {

WifiController& WifiController::get_instance() {
  static WifiController instance;
  return instance;
}

esp_err_t WifiController::init_sta() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_.load(std::memory_order_acquire) !=
      connection_state::UNINITIALIZED) {
    ESP_LOGI(TAG, "STA is ready for Wi-Fi connection, no need to initialize");
    return ESP_OK;
  }

  last_error_.store(connection_error::NONE, std::memory_order_release);
  retry_num_ = 0;

  esp_err_t ret = setup_netif_and_event_group();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to setup network interface and events");
    set_connection_error(connection_error::UNKNOWN_ERROR);
    return ret;
  }

  ESP_LOGI(TAG, "Wi-Fi station initialized");
  return ESP_OK;
}

esp_err_t WifiController::connect(const char* ssid, const char* password) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!validate_credentials(ssid, password)) {
    return ESP_ERR_INVALID_ARG;
  }

  char* stored_ssid = current_ssid_.load(std::memory_order_acquire);
  if (stored_ssid != nullptr && strcmp(stored_ssid, ssid) == 0 &&
      state_.load(std::memory_order_acquire) == connection_state::CONNECTED) {
    ESP_LOGI(TAG, "Already connected to SSID: %s", ssid);
    return ESP_OK;
  }

  if (state_.load(std::memory_order_acquire) == connection_state::CONNECTED) {
    ESP_LOGI(TAG, "Disconnecting from current SSID: %s",
             stored_ssid ? stored_ssid : "");
    if (disconnect() != ESP_OK) {
      return ESP_FAIL;
    }
  }

  if (stored_ssid != nullptr) {
    free(stored_ssid);
    current_ssid_.store(nullptr, std::memory_order_release);
  }

  char* new_ssid = strdup(ssid);
  current_ssid_.store(new_ssid, std::memory_order_release);

  wifi_config_t wifi_config{};
  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid,
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password,
          sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  state_.store(connection_state::CONNECTING, std::memory_order_release);
  ESP_LOGI(TAG, "Attempting to connect to SSID: %s", ssid);
  return ESP_OK;
}

esp_err_t WifiController::disconnect() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_.load(std::memory_order_acquire) ==
          connection_state::DISCONNECTED ||
      state_.load(std::memory_order_acquire) ==
          connection_state::UNINITIALIZED) {
    ESP_LOGI(TAG, "Wi-Fi is already disconnected");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
  esp_err_t ret = esp_wifi_disconnect();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(ret));
    return ret;
  }

  state_.store(connection_state::DISCONNECTED, std::memory_order_release);
  last_error_.store(connection_error::NONE, std::memory_order_release);

  char* stored_ssid = current_ssid_.load(std::memory_order_acquire);
  if (stored_ssid) {
    free(stored_ssid);
    current_ssid_.store(nullptr, std::memory_order_release);
  }

  char* stored_ip = ip_address_.load(std::memory_order_acquire);
  if (stored_ip) {
    free(stored_ip);
    ip_address_.store(nullptr, std::memory_order_release);
  }

  return ESP_OK;
}

void WifiController::set_connection_error(connection_error error) {
  last_error_.store(error, std::memory_order_release);
  state_.store(connection_state::FAILED, std::memory_order_release);
  ESP_LOGE(TAG, "Connection error occured");
}

esp_err_t WifiController::setup_netif_and_event_group() {
  ESP_ERROR_CHECK(controller::ensure_nvs());

  wifi_event_group_ = xEventGroupCreate();
  ESP_LOGI(TAG, "Wi-Fi event group created");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  ESP_LOGI(TAG, "Network interface setup completed");

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  esp_event_handler_instance_t wifi_instance;
  esp_event_handler_instance_t ip_instance;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, &wifi_instance));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, this, &ip_instance));

  ESP_LOGI(TAG, "Event handlers registered successfully");
  return ESP_OK;
}

bool WifiController::validate_credentials(const char* ssid,
                                          const char* password) {
  if (ssid == nullptr || strlen(ssid) == 0) {
    ESP_LOGE(TAG, "Wi-Fi SSID is empty");
    set_connection_error(connection_error::INVALID_CREDENTIALS);
    return false;
  }

  if (password == nullptr || strlen(password) == 0) {
    ESP_LOGE(TAG, "Wi-Fi password is empty");
    set_connection_error(connection_error::INVALID_CREDENTIALS);
    return false;
  }

  return true;
}

void WifiController::handle_wifi_event(int32_t event_id, void* event_data) {
  switch (event_id) {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "Wi-Fi station started, attempting to connect");
      state_.store(connection_state::CONNECTING, std::memory_order_release);
      ESP_ERROR_CHECK(esp_wifi_connect());
      break;

    case WIFI_EVENT_STA_DISCONNECTED: {
      wifi_event_sta_disconnected_t* disconnected =
          static_cast<wifi_event_sta_disconnected_t*>(event_data);

      ESP_LOGW(TAG, "%s disconnected. Reason number: %d", disconnected->ssid,
               disconnected->reason);

      connection_error err = connection_error::UNKNOWN_ERROR;
      switch (disconnected->reason) {
        case WIFI_REASON_NO_AP_FOUND:
          err = connection_error::SSID_NOT_FOUND;
          break;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_EXPIRE:
          err = connection_error::AUTHENTICATION_FAILED;
          break;
        default:
          err = connection_error::UNKNOWN_ERROR;
          break;
      }

      set_connection_error(err);

      if (retry_num_ < 5) {
        ESP_LOGI(TAG, "Retrying to connect to the AP... (attempt %zu/%zu)",
                 retry_num_ + 1, 5);
        ESP_ERROR_CHECK(esp_wifi_connect());
        retry_num_++;
      } else {
        xEventGroupSetBits(wifi_event_group_, WIFI_FAIL_BIT);
        ESP_LOGE(TAG, "Failed to connect to the AP after %zu attempts", 5);
      }
      break;
    }

    default:
      break;
  }
}

void WifiController::handle_ip_event(int32_t event_id, void* event_data) {
  if (event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&event->ip_info.ip));

    ip_address_.store(ip_addr, std::memory_order_release);

    retry_num_ = 0;
    last_error_.store(connection_error::NONE, std::memory_order_release);
    state_.store(connection_state::CONNECTED, std::memory_order_release);

    xEventGroupSetBits(wifi_event_group_, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Connected successfully! IP address: %s", ip_addr);
  }
}

connection_state WifiController::get_connection_state() {
  return state_.load(std::memory_order_acquire);
}

connection_error WifiController::get_last_error() {
  return last_error_.load(std::memory_order_acquire);
}

WifiController::~WifiController() {
  char* stored_ssid = current_ssid_.load(std::memory_order_acquire);
  if (stored_ssid) {
    free(stored_ssid);
  }

  char* stored_ip = ip_address_.load(std::memory_order_acquire);
  if (stored_ip) {
    free(stored_ip);
  }

  if (wifi_event_group_) {
    vEventGroupDelete(wifi_event_group_);
  }
}

esp_err_t wifi_connect(const char* ssid, const char* password) {
  auto& instance = WifiController::get_instance();
  instance.init_sta();
  return instance.connect(ssid, password);
}

}  // namespace controller
