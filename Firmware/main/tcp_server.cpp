#include "tcp_server.hpp"

namespace server {

TcpServer::TcpServer() {
  clients_.reserve(MAX_CLIENTS);
}

TcpServer& TcpServer::get_instance() {
  static TcpServer instance;
  return instance;
}

void TcpServer::set_message_handler(client_handler_t handler) {
  message_handler_ = handler;
}

void TcpServer::set_on_connect_handler(on_connect_handler_t handler) {
  on_connect_handler_ = handler;
}

void TcpServer::set_on_disconnect_handler(on_disconnect_handler_t handler) {
  on_disconnect_handler_ = handler;
}

void TcpServer::start(uint16_t port) {
  if (!message_handler_) {
    ESP_LOGE(TAG, "No message handler set, server stopped");
    return;
  }

  xTaskCreate(tcp_server_task, "main_tcp_server", 8192, (void*)(uintptr_t)port,
              5, NULL);
}

void TcpServer::tcp_server_task(void* pvParameters) {
  uint16_t port = (uint16_t)(uintptr_t)pvParameters;
  auto& server = TcpServer::get_instance();

  int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_sock < 0) {
    ESP_LOGE(TAG, "Socket creation failed: %s", strerror(errno));
    vTaskDelete(NULL);
    return;
  }

  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in dest_addr = {};
  dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
    ESP_LOGE(TAG, "Bind failed: %s", strerror(errno));
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }

  if (listen(listen_sock, 20) < 0) {
    ESP_LOGE(TAG, "Listen failed: %s", strerror(errno));
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }

  if (server.set_nonblocking(listen_sock) < 0) {
    ESP_LOGE(TAG, "Failed to set listen sokcet to non-blocking");
    close(listen_sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Server listening o port %d", port);
  server.handle_clients(listen_sock);

  // cleanup
  for (auto& client : server.clients_) {
    if (client.active) {
      close(client.sock);
    }
  }
  close(listen_sock);
  vTaskDelete(NULL);
}

void TcpServer::handle_clients(int listen_sock) {
  fd_set read_fds;
  struct timeval timeout;
  time_t last_cleanup = 0;

  while (true) {
    FD_ZERO(&read_fds);
    FD_SET(listen_sock, &read_fds);

    int max_fd = listen_sock;

    for (auto& client : clients_) {
      if (client.active) {
        FD_SET(client.sock, &read_fds);
        max_fd = std::max(max_fd, client.sock);
      }
    }

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (activity < 0) {
      ESP_LOGE(TAG, "Select error: %s", strerror(errno));
      break;
    }

    if (FD_ISSET(listen_sock, &read_fds)) {
      handle_new_connection(listen_sock);
    }

    for (auto& client : clients_) {
      if (client.active && FD_ISSET(client.sock, &read_fds)) {
        handle_client_data(client);
      }
    }

    // periodic cleanup
    time_t now = time(NULL);
    if (now - last_cleanup >= cleanup_interval_seconds_) {
      cleanup_inactive_clients();
      last_cleanup = now;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void TcpServer::handle_new_connection(int listen_sock) {
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int client_sock =
      accept(listen_sock, (struct sockaddr*)&client_addr, &addr_len);

  if (client_sock < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGE(TAG, "Accpet failed: %s", strerror(errno));
    }
    return;
  }

  if (set_nonblocking(client_sock) < 0) {
    ESP_LOGW(TAG, "Failed to set client socket non-blocking");
    close(client_sock);
    return;
  }

  struct timeval sock_timeout = {.tv_sec = (time_t)client_timeout_seconds_,
                                 .tv_usec = 0};
  setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout,
             sizeof(sock_timeout));
  setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout,
             sizeof(sock_timeout));

  bool added = false;
  for (auto& client : clients_) {
    if (!client.active) {
      client.sock = client_sock;
      client.active = true;
      client.last_activity = time(NULL);
      client.addr = client_addr;
      added = true;
      break;
    }
  }

  if (!added && clients_.size() < MAX_CLIENTS) {
    clients_.push_back({client_sock, true, time(NULL), client_addr});
    added = true;
  }

  if (added) {
    ESP_LOGD("TCP", "Client connected from %s:%d, socket %d",
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
             client_sock);

    if (on_connect_handler_) {
      on_connect_handler_(client_sock, client_addr);
    }
  } else {
    ESP_LOGW("TCP", "Max clients (%d) reached, rejecting connection from %s:%d",
             MAX_CLIENTS, inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port));
    close(client_sock);
  }
};

void TcpServer::handle_client_data(client_info_t& client) {
  char* buffer = new char[buffer_size_];

  ssize_t rlen = recv(client.sock, buffer, buffer_size_ - 1, MSG_DONTWAIT);

  if (rlen > 0) {
    buffer[rlen] = '\0';
    client.last_activity = time(NULL);

    char* response;
    bool is_success = false;

    if (message_handler_) {
      const size_t max_response_size = 1024 * 64;
      response = const_cast<char*>(message_handler_(buffer, rlen, client.sock));

      if (strlen(response) <= max_response_size) {
        is_success = true;
      } else {
        ESP_LOGE(TAG,
                 "Handler returned oversized response (%d bytes) for socket %d",
                 (int)strlen(response), client.sock);
        response[0] = '\0';
        is_success = false;
      }
    } else {
      ESP_LOGE(TAG, "No message handler set for socket %d", client.sock);
      is_success = false;
    }

    if (!is_success) {
      // Handler failed, close connection
      ESP_LOGD("TCP", "Handler failed for socket %d, closing connection",
               client.sock);
      if (on_disconnect_handler_) {
        on_disconnect_handler_(client.sock);
      }
      close(client.sock);
      client.active = false;
    } else if (strlen(response) == 0) {
      // Handler wants to close connection
      ESP_LOGD("TCP", "Handler requested connection close for socket %d",
               client.sock);
      if (on_disconnect_handler_) {
        on_disconnect_handler_(client.sock);
      }
      close(client.sock);
      client.active = false;
    } else {
      // Send response
      ssize_t sent =
          send(client.sock, response, strlen(response), MSG_DONTWAIT);
      if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGD("TCP", "Send failed for socket %d: %s", client.sock,
                 strerror(errno));
        if (on_disconnect_handler_) {
          on_disconnect_handler_(client.sock);
        }
        close(client.sock);
        client.active = false;
      } else if (sent < (ssize_t)strlen(response)) {
        ESP_LOGW("TCP", "Partial send: %d/%d bytes for socket %d", (int)sent,
                 (int)strlen(response), client.sock);
      }
    }
  } else if (rlen == 0 ||
             (rlen < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    // Connection closed or error
    ESP_LOGD("TCP", "Client disconnected, socket %d: %s", client.sock,
             rlen == 0 ? "connection closed" : strerror(errno));
    if (on_disconnect_handler_) {
      on_disconnect_handler_(client.sock);
    }
    close(client.sock);
    client.active = false;
  }

  delete[] buffer;
}

int TcpServer::set_nonblocking(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0)
    return -1;
  return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void TcpServer::cleanup_inactive_clients() {
  time_t now = time(NULL);

  for (auto& client : clients_) {
    if (client.active &&
        (now - client.last_activity) > client_timeout_seconds_) {
      ESP_LOGD("TCP", "Client timeout, closing socket %d", client.sock);
      if (on_disconnect_handler_) {
        on_disconnect_handler_(client.sock);
      }
      close(client.sock);
      client.active = false;
    }
  }
}

void set_message_handler(client_handler_t handler) {
  TcpServer::get_instance().set_message_handler(handler);
}

void launch(uint16_t port) {
  TcpServer::get_instance().start(port);
}

}  // namespace server