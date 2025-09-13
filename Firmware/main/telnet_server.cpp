#include "telnet_server.hpp"
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "usb_cdc_manager.hpp"

namespace telnet_server {

// Telnet command constants
static constexpr const uint8_t TELNET_IAC = 255;   // Interpret As Command
static constexpr const uint8_t TELNET_DONT = 254;  // Don't
static constexpr const uint8_t TELNET_DO = 253;    // Do
static constexpr const uint8_t TELNET_WONT = 252;  // Won't
static constexpr const uint8_t TELNET_WILL = 251;  // Will
static constexpr const uint8_t TELNET_SB = 250;    // Subnegotiation Begin
static constexpr const uint8_t TELNET_SE = 240;    // Subnegotiation End
static constexpr const uint8_t TELNET_ECHO = 1;    // Echo option
static constexpr const uint8_t TELNET_SUPPRESS_GO_AHEAD =
    3;  // Suppress Go Ahead option

// Global server instance
static std::unique_ptr<TelnetServer> g_server_instance = nullptr;

TelnetServer::TelnetServer(uint16_t port)
    : port_(port),
      running_(false),
      client_count_(0),
      server_socket_(-1),
      server_task_handle_(nullptr) {
  clients_.resize(MAX_CLIENTS);
  for (auto& client : clients_) {
    client.socket_fd = -1;
    client.task_handle = nullptr;
    client.active = false;
    client.usb_forwarding_mode = false;
    client.server = nullptr;
  }
}

TelnetServer::~TelnetServer() {
  stop();
}

esp_err_t TelnetServer::start() {
  if (running_.load()) {
    ESP_LOGW(TAG, "Server is already running");
    return ESP_OK;
  }

  // Create server socket
  server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_socket_ < 0) {
    ESP_LOGE(TAG, "Failed to create server socket: %s", strerror(errno));
    return ESP_FAIL;
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    ESP_LOGE(TAG, "Failed to set socket options: %s", strerror(errno));
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  // Set socket to non-blocking mode for better control
  int flags = fcntl(server_socket_, F_GETFL, 0);
  if (flags < 0) {
    ESP_LOGE(TAG, "Failed to get socket flags: %s", strerror(errno));
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  if (fcntl(server_socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
    ESP_LOGE(TAG, "Failed to set socket to non-blocking: %s", strerror(errno));
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  // Bind socket to address
  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port_);

  if (bind(server_socket_, (struct sockaddr*)&server_addr,
           sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket to port %d: %s", port_,
             strerror(errno));
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  // Start listening
  if (listen(server_socket_, MAX_CLIENTS) < 0) {
    ESP_LOGE(TAG, "Failed to listen on socket: %s", strerror(errno));
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  // Set running flag before creating task
  running_.store(true);

  // Create server task
  BaseType_t ret = xTaskCreate(server_task, "telnet_server", TASK_STACK_SIZE,
                               this, TASK_PRIORITY, &server_task_handle_);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create server task");
    running_.store(false);
    close(server_socket_);
    server_socket_ = -1;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Telnet server started on port %d", port_);
  return ESP_OK;
}

esp_err_t TelnetServer::stop() {
  if (!running_.load()) {
    return ESP_OK;
  }

  running_.store(false);

  // Close all client connections
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients_[i].active) {
      remove_client(i);
    }
  }

  // Close server socket
  if (server_socket_ >= 0) {
    close(server_socket_);
    server_socket_ = -1;
  }

  // Wait for server task to finish
  if (server_task_handle_) {
    vTaskDelete(server_task_handle_);
    server_task_handle_ = nullptr;
  }

  ESP_LOGI(TAG, "Telnet server stopped");
  return ESP_OK;
}

bool TelnetServer::is_running() const {
  return running_.load();
}

uint16_t TelnetServer::get_port() const {
  return port_;
}

size_t TelnetServer::get_client_count() const {
  return client_count_.load();
}

void TelnetServer::server_task(void* pvParameters) {
  TelnetServer* server = static_cast<TelnetServer*>(pvParameters);

  ESP_LOGI(TAG, "Server task started, waiting for connections on socket %d...",
           server->server_socket_);
  ESP_LOGI(TAG, "Running flag: %s", server->running_.load() ? "true" : "false");

  // Small delay to ensure everything is initialized
  vTaskDelay(pdMS_TO_TICKS(100));

  while (server->running_.load()) {
    struct sockaddr_in client_addr = {};
    socklen_t client_len = sizeof(client_addr);

    // ESP_LOGI(TAG, "Waiting for client connection...");
    int client_socket = accept(server->server_socket_,
                               (struct sockaddr*)&client_addr, &client_len);

    if (client_socket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      ESP_LOGE(TAG, "Failed to accept client connection: %s (errno: %d)",
               strerror(errno), errno);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    ESP_LOGI(TAG, "Client connection accepted on socket %d", client_socket);

    // Find available client slot
    int client_index = server->find_available_client_slot();
    if (client_index < 0) {
      ESP_LOGW(TAG, "No available client slots, rejecting connection");
      close(client_socket);
      continue;
    }

    // Store client information
    server->clients_[client_index].socket_fd = client_socket;
    server->clients_[client_index].active = true;
    server->clients_[client_index].usb_forwarding_mode = false;
    server->clients_[client_index].client_ip = inet_ntoa(client_addr.sin_addr);
    server->clients_[client_index].server = server;  // Set server pointer
    server->client_count_.fetch_add(1);

    // Create client task
    BaseType_t ret = xTaskCreate(client_task, "telnet_client", TASK_STACK_SIZE,
                                 &server->clients_[client_index], TASK_PRIORITY,
                                 &server->clients_[client_index].task_handle);

    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create client task");
      server->remove_client(client_index);
      continue;
    }

    ESP_LOGI(TAG, "Client connected from %s (slot %d)",
             server->clients_[client_index].client_ip.c_str(), client_index);
  }

  ESP_LOGI(TAG, "Server task ended");
  vTaskDelete(nullptr);
}

void TelnetServer::client_task(void* pvParameters) {
  ClientInfo* client_info = static_cast<ClientInfo*>(pvParameters);
  TelnetServer* server = client_info->server;

  if (!server) {
    ESP_LOGE(TAG, "Server instance not found in client info");
    vTaskDelete(nullptr);
    return;
  }

  char buffer[BUFFER_SIZE];
  std::string welcome_msg = "Welcome to Fluidity Telnet Server!\r\n";
  welcome_msg += "Type 'help' for available commands.\r\n";
  welcome_msg += "> ";

  server->send_response(*client_info, welcome_msg);

  ESP_LOGI(TAG, "Client task started for %s on socket %d",
           client_info->client_ip.c_str(), client_info->socket_fd);

  while (client_info->active) {
    ESP_LOGI(TAG, "Waiting for data from client %s...",
             client_info->client_ip.c_str());

    int bytes_received =
        recv(client_info->socket_fd, buffer, sizeof(buffer) - 1, 0);

    ESP_LOGI(TAG, "recv returned %d bytes", bytes_received);

    if (bytes_received <= 0) {
      if (bytes_received == 0) {
        ESP_LOGI(TAG, "Client %s disconnected", client_info->client_ip.c_str());
      } else {
        ESP_LOGE(TAG, "Error receiving data from client %s: %s (errno: %d)",
                 client_info->client_ip.c_str(), strerror(errno), errno);
      }
      break;
    }

    buffer[bytes_received] = '\0';

    ESP_LOGI(TAG, "Raw data received (%d bytes):", bytes_received);
    for (int j = 0; j < bytes_received; j++) {
      if (buffer[j] >= 32 && buffer[j] <= 126) {
        ESP_LOGI(TAG, "  [%d] = '%c' (0x%02x)", j, buffer[j],
                 (unsigned char)buffer[j]);
      } else {
        ESP_LOGI(TAG, "  [%d] = 0x%02x", j, (unsigned char)buffer[j]);
      }
    }

    // Check if we're in USB forwarding mode
    if (client_info->usb_forwarding_mode) {
      // Forward raw data to USB device
      ESP_LOGI(TAG, "Forwarding %d bytes to USB device", bytes_received);
      esp_err_t ret =
          usb_cdc_manager::send_data((const uint8_t*)buffer, bytes_received);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to USB device: %s",
                 esp_err_to_name(ret));
        // Exit USB forwarding mode on error
        client_info->usb_forwarding_mode = false;
        std::string error_msg =
            "USB forwarding error, returning to command mode\r\n> ";
        server->send_response(*client_info, error_msg);
      }
    } else {
      // Normal command processing
      std::string input;
      for (int j = 0; j < bytes_received; j++) {
        if (buffer[j] >= 32 && buffer[j] <= 126) {  // Printable characters
          input += buffer[j];
        } else if (buffer[j] == '\r' || buffer[j] == '\n') {
          // End of line
          if (!input.empty()) {
            ESP_LOGI(TAG, "Received command from %s: '%s'",
                     client_info->client_ip.c_str(), input.c_str());
            server->handle_client_data(*client_info, input);
          }
          input.clear();
        }
      }
    }
  }

  // Find and remove this client
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (&server->clients_[i] == client_info) {
      server->remove_client(i);
      break;
    }
  }

  vTaskDelete(nullptr);
}

void TelnetServer::handle_client_data(ClientInfo& client_info,
                                      const std::string& input) {
  ESP_LOGI(TAG, "Processing command from %s: '%s'",
           client_info.client_ip.c_str(), input.c_str());

  std::string response;

  if (input == "help") {
    response = "Available commands:\r\n";
    response += "  help     - Show this help message\r\n";
    response += "  status   - Show system status\r\n";
    response += "  uptime   - Show system uptime\r\n";
    response += "  free     - Show free memory\r\n";
    response += "  echo <text> - Echo back the text\r\n";
    response += "  list     - List all USB devices with VID/PID\r\n";
    response += "  scan     - Scan for USB CDC ACM devices\r\n";
    response += "  serial   - Connect to USB CDC ACM device\r\n";
    response += "  quit     - Disconnect\r\n";
  } else if (input == "status") {
    response = "System Status:\r\n";
    response += "  WiFi: Connected\r\n";
    response += "  Clients: " + std::to_string(client_count_.load()) + "\r\n";
    response += "  Port: " + std::to_string(port_) + "\r\n";
  } else if (input == "uptime") {
    uint32_t uptime_ms = esp_timer_get_time() / 1000;
    uint32_t hours = uptime_ms / 3600000;
    uint32_t minutes = (uptime_ms % 3600000) / 60000;
    uint32_t seconds = (uptime_ms % 60000) / 1000;

    response = "Uptime: " + std::to_string(hours) + "h " +
               std::to_string(minutes) + "m " + std::to_string(seconds) +
               "s\r\n";
  } else if (input == "free") {
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    response = "Free heap: " + std::to_string(free_heap) + " bytes\r\n";
    response +=
        "Min free heap: " + std::to_string(min_free_heap) + " bytes\r\n";
  } else if (input.substr(0, 4) == "echo") {
    if (input.length() > 5) {
      response = "Echo: " + input.substr(5) + "\r\n";
    } else {
      response = "Usage: echo <text>\r\n";
    }
  } else if (input == "list") {
    // Initialize USB CDC manager if not already done
    if (!usb_cdc_manager::is_initialized()) {
      esp_err_t ret = usb_cdc_manager::init();
      if (ret != ESP_OK) {
        response = "Failed to initialize USB CDC manager: " +
                   std::string(esp_err_to_name(ret)) + "\r\n";
      } else {
        response = "USB CDC manager initialized\r\n";
        response += usb_cdc_manager::list_all_usb_devices();
      }
    } else {
      response = usb_cdc_manager::list_all_usb_devices();
    }
  } else if (input == "scan") {
    // Initialize USB CDC manager if not already done
    if (!usb_cdc_manager::is_initialized()) {
      esp_err_t ret = usb_cdc_manager::init();
      if (ret != ESP_OK) {
        response = "Failed to initialize USB CDC manager: " +
                   std::string(esp_err_to_name(ret)) + "\r\n";
      } else {
        response = "USB CDC manager initialized\r\n";
        response += usb_cdc_manager::scan_devices();
      }
    } else {
      response = usb_cdc_manager::scan_devices();
    }
  } else if (input == "serial") {
    // Initialize USB CDC manager if not already done
    if (!usb_cdc_manager::is_initialized()) {
      esp_err_t ret = usb_cdc_manager::init();
      if (ret != ESP_OK) {
        response = "Failed to initialize USB CDC manager: " +
                   std::string(esp_err_to_name(ret)) + "\r\n";
      } else {
        response = "USB CDC manager initialized\r\n";
      }
    }

    if (usb_cdc_manager::is_initialized()) {
      // Try to open a CDC ACM device (try multiple common devices)
      esp_err_t ret = ESP_ERR_NOT_FOUND;
      
      // Try CH340 USB-to-serial chip variants (most common)
      ret = usb_cdc_manager::open_device(0x1A86, 0x7523, 0);  // CH340
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x1A86, 0x5523, 0);  // CH340 variant
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x1A86, 0x7522, 0);  // CH341
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x1A86, 0x7524, 0);  // CH340G
      }
      if (ret != ESP_OK) {
        // Try CP2102 variants
        ret = usb_cdc_manager::open_device(0x10C4, 0xEA60, 0);  // CP2102
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x10C4, 0xEA61, 0);  // CP2104
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x10C4, 0xEA70, 0);  // CP2102N
      }
      if (ret != ESP_OK) {
        // Try FTDI variants
        ret = usb_cdc_manager::open_device(0x0403, 0x6001, 0);  // FTDI FT232
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x0403, 0x6015, 0);  // FTDI FT X-Series
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x0403, 0x6010, 0);  // FTDI FT2232
      }
      if (ret != ESP_OK) {
        // Try Arduino variants
        ret = usb_cdc_manager::open_device(0x2341, 0x0043, 0);  // Arduino Uno
      }
      if (ret != ESP_OK) {
        ret = usb_cdc_manager::open_device(0x2341, 0x0001, 0);  // Arduino Uno variant
      }
      if (ret != ESP_OK) {
        // Try TinyUSB CDC device (from example)
        ret = usb_cdc_manager::open_device(0x303A, 0x4001, 0);  // TinyUSB CDC device
      }
      if (ret != ESP_OK) {
        // Try TinyUSB Dual CDC device
        ret = usb_cdc_manager::open_device(0x303A, 0x4002, 0);  // TinyUSB Dual CDC device
      }

      if (ret == ESP_OK) {
        // Set up USB data callback for this client
        usb_cdc_manager::set_data_callback(
            [this, &client_info](const uint8_t* data, size_t len) {
              handle_usb_data_forwarding(client_info, data, len);
            });

        client_info.usb_forwarding_mode = true;
        response =
            "Connected to USB CDC ACM device. All data will be forwarded.\r\n";
        response += "Type 'exit' to return to command mode.\r\n";
      } else {
        response = "Failed to open USB CDC ACM device: " +
                   std::string(esp_err_to_name(ret)) + "\r\n";
        response += "Make sure a CDC ACM device is connected.\r\n";
      }
    }
  } else if (input == "exit" && client_info.usb_forwarding_mode) {
    // Exit USB forwarding mode
    client_info.usb_forwarding_mode = false;
    usb_cdc_manager::close_device();
    response = "Disconnected from USB device, returning to command mode\r\n";
  } else if (input == "quit") {
    response = "Goodbye!\r\n";
    send_response(client_info, response);
    client_info.active = false;
    return;
  } else if (input.empty()) {
    // Empty input, just show prompt
    response = "> ";
  } else {
    response = "Unknown command: '" + input + "'\r\n";
    response += "Type 'help' for available commands.\r\n";
  }

  response += "> ";
  send_response(client_info, response);
}

void TelnetServer::handle_usb_data_forwarding(ClientInfo& client_info,
                                              const uint8_t* data,
                                              size_t len) {
  if (!client_info.active || client_info.socket_fd < 0) {
    return;
  }

  ESP_LOGI(TAG, "Forwarding %zu bytes from USB device to client %s", len,
           client_info.client_ip.c_str());

  // Send the USB data directly to the telnet client
  int bytes_sent = send(client_info.socket_fd, data, len, 0);
  if (bytes_sent < 0) {
    ESP_LOGE(TAG, "Failed to forward USB data to client %s: %s",
             client_info.client_ip.c_str(), strerror(errno));
  } else {
    ESP_LOGI(TAG, "Successfully forwarded %d bytes to client %s", bytes_sent,
             client_info.client_ip.c_str());
  }
}

esp_err_t TelnetServer::send_response(ClientInfo& client_info,
                                      const std::string& message) {
  if (!client_info.active || client_info.socket_fd < 0) {
    ESP_LOGE(TAG, "Cannot send response: client not active or invalid socket");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Sending response to %s (%d bytes): '%s'",
           client_info.client_ip.c_str(), (int)message.length(),
           message.c_str());

  int bytes_sent =
      send(client_info.socket_fd, message.c_str(), message.length(), 0);
  if (bytes_sent < 0) {
    ESP_LOGE(TAG, "Failed to send response to client %s: %s (errno: %d)",
             client_info.client_ip.c_str(), strerror(errno), errno);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Successfully sent %d bytes to client %s", bytes_sent,
           client_info.client_ip.c_str());
  return ESP_OK;
}

int TelnetServer::find_available_client_slot() {
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (!clients_[i].active) {
      return i;
    }
  }
  return -1;
}

void TelnetServer::remove_client(int client_index) {
  if (client_index < 0 || client_index >= MAX_CLIENTS ||
      !clients_[client_index].active) {
    return;
  }

  ESP_LOGI(TAG, "Removing client %s (slot %d)",
           clients_[client_index].client_ip.c_str(), client_index);

  // Close socket
  if (clients_[client_index].socket_fd >= 0) {
    close(clients_[client_index].socket_fd);
  }

  // Delete task
  if (clients_[client_index].task_handle) {
    vTaskDelete(clients_[client_index].task_handle);
  }

  // Reset client info
  clients_[client_index].socket_fd = -1;
  clients_[client_index].task_handle = nullptr;
  clients_[client_index].active = false;
  clients_[client_index].usb_forwarding_mode = false;
  clients_[client_index].client_ip.clear();
  clients_[client_index].server = nullptr;

  client_count_.fetch_sub(1);
}

// Global functions
esp_err_t launch(uint16_t port) {
  if (g_server_instance) {
    ESP_LOGW("Telnet Server", "Server is already running");
    return ESP_OK;
  }

  g_server_instance = std::make_unique<TelnetServer>(port);
  return g_server_instance->start();
}

esp_err_t stop() {
  if (!g_server_instance) {
    return ESP_OK;
  }

  esp_err_t ret = g_server_instance->stop();
  g_server_instance.reset();
  return ret;
}

bool is_running() {
  return g_server_instance && g_server_instance->is_running();
}

size_t get_client_count() {
  return g_server_instance ? g_server_instance->get_client_count() : 0;
}

}  // namespace telnet_server
