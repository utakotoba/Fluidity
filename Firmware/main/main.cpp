#include "nvs_controller.hpp"
#include "sdkconfig.h"
#include "wifi_controller.hpp"

// #define HTTP_RESPONSE            \
//   "HTTP/1.1 200 OK\r\n"          \
//   "Content-Type: text/plain\r\n" \
//   "Content-Length: 11\r\n"       \
//   "\r\n"                         \
//   "Hello World"

// auto simple_handler = [](const char* data, size_t length, int client_socket)
// {
//   return HTTP_RESPONSE;
// };

extern "C" void app_main() {
  controller::ensure_nvs();
  controller::wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
}
