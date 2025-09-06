idf_component_register(
  SRCS "main.cpp" "nvs_factory.cpp" "wifi_controller.cpp"
  REQUIRES nvs_flash esp_wifi esp_event
  INCLUDE_DIRS "./include"
)
