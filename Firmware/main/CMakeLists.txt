set(COMPONENT_ADD_INCLUDEDIRS "./include")
set(
  COMPONENT_SRCS
  "main.cpp"
  "nvs_controller.cpp"
  "wifi_controller.cpp"
)
set(
  COMPONENT_REQUIRES
  nvs_flash
  esp_wifi
  esp_event
)

register_component()
