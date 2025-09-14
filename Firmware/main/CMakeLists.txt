set(COMPONENT_ADD_INCLUDEDIRS "./include")
set(
  COMPONENT_SRCS
  "main.cpp"
  "nvs_controller.cpp"
)
set(
  COMPONENT_REQUIRES
  nvs_flash
)

register_component()
