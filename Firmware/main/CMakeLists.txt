set(COMPONENT_ADD_INCLUDEDIRS "./include")
set(
  COMPONENT_SRCS
  "main.cpp"
  "nvs_controller.cpp"
  "wifi_controller.cpp"
  "tcp_server_unit.cpp"
)
set(
  COMPONENT_REQUIRES
  nvs_flash
  esp_wifi
  esp_event
  esp_psram
)

register_component()
