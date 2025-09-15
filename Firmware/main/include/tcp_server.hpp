#pragma once

#include <lwip/sockets.h>
#include <ctime>
#include <functional>
#include "esp_log.h"

#define MAX_CLIENTS 64

namespace server {

using client_handler_t = std::function<
    const char*(const char* data, size_t length, int client_socket)>;

using on_connect_handler_t =
    std::function<void(int client_socket,
                       const struct sockaddr_in& client_addr)>;

using on_disconnect_handler_t = std::function<void(int client_socket)>;

struct client_info_t {
  int sock;
  bool active;
  time_t last_activity;
  struct sockaddr_in addr;
};

class TcpServer {
 public:
  static TcpServer& get_instance();

  void set_message_handler(client_handler_t handler);
  void set_on_connect_handler(on_connect_handler_t handler);
  void set_on_disconnect_handler(on_disconnect_handler_t handler);

  void start(uint16_t port);

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

 private:
  TcpServer();
  // ~TcpServer();

  static constexpr const char* TAG = "tcp_server";
  std::vector<client_info_t> clients_;
  client_handler_t message_handler_;
  on_connect_handler_t on_connect_handler_;
  on_disconnect_handler_t on_disconnect_handler_;

  uint32_t client_timeout_seconds_{60};
  uint32_t cleanup_interval_seconds_{10};
  size_t buffer_size_{1536};

  static void tcp_server_task(void* pvParameters);
  void handle_clients(int listenn_sock);
  void handle_new_connection(int listen_sock);
  void handle_client_data(client_info_t& client);

  int set_nonblocking(int sock);
  void cleanup_inactive_clients();
};

void set_message_handler(client_handler_t handler);
void launch(uint16_t port);

}  // namespace server
