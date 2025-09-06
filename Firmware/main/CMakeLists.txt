idf_component_register(
  SRCS "main.cpp" "nvs_factory.cpp"
  REQUIRES nvs_flash
  INCLUDE_DIRS "./include"
)
