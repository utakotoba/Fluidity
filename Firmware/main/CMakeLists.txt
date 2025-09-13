idf_component_register(
  SRCS "main.cpp" "nvs_factory.cpp" "wifi_controller.cpp" "telnet_server.cpp" "usb_cdc_manager.cpp"
  REQUIRES nvs_flash esp_wifi esp_event lwip esp_timer usb
  INCLUDE_DIRS "./include"
)
