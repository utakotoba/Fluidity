#include "wifi_controller.hpp"
#include "esp_log.h"

namespace wifi_controller {
Controller::~Controller() {
  if (wifi_event_group_) {
    ESP_LOGI(TAG, "Deleting Wi-Fi event group...");
    vEventGroupDelete(wifi_event_group_);
    ESP_LOGI(TAG, "Wi-Fi event group is successfully deleted");
  }
}

void Controller::handle_event(esp_event_base_t event_base,
                              int32_t event_id,
                              void* event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        if (retry_num_ < WIFI_MAXIMUM_RETRY) {
          ESP_LOGI(TAG, "Retrying to connect tp the AP...");
          ESP_ERROR_CHECK(esp_wifi_connect());
          retry_num_++;
        } else {
          xEventGroupSetBits(wifi_event_group_, WIFI_FAIL_BIT);
          ESP_LOGW(TAG, "Failed to connect to the AP");
        }
        break;
      default:
        break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
    char ip_addr[16];
    snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&event->ip_info.ip));

    ip_address_.store(std::make_shared<std::string>(ip_addr),
                      std::memory_order_release);

    retry_num_ = 0;
    xEventGroupSetBits(wifi_event_group_, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "Connected, got IP address: %s", ip_addr);
  }
}

esp_err_t Controller::init_sta() {
  nvs_factory::ensure_initialized();
  wifi_event_group_ = xEventGroupCreate();
  ESP_LOGI(TAG, "Wi-Fi event group created");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  ESP_LOGI(TAG, "Netif setup successfully");

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, this,
      &instance_got_ip));
  ESP_LOGI(TAG, "Event handlers registered successfully");

  wifi_config_t wifi_config = {};
  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), WIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strncpy(reinterpret_cast<char*>(wifi_config.sta.password), WIFI_PASSWORD,
          sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "Wi-Fi init finished");

  EventBits_t bits =
      xEventGroupWaitBits(wifi_event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to AP SSID: %s", WIFI_SSID);
    return ESP_OK;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    return ESP_OK;
  }
  ESP_LOGE(TAG, "Unexpected event");
  return ESP_ERR_INVALID_STATE;
}

std::shared_ptr<std::string> Controller::get_ip_address() {
  return ip_address_.load(std::memory_order_acquire);
}

esp_err_t launch() {
  return Controller::get_instance().init_sta();
}

std::string get_ip_address() {
  auto ip_addr_ptr = Controller::get_instance().get_ip_address();
  return ip_addr_ptr ? *ip_addr_ptr : std::string{};
}
}  // namespace wifi_controller