#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

/**
 * @file telnet_server.hpp
 * @brief Simple telnet server for ESP32 with basic client communication
 */

namespace telnet_server {

/**
 * @brief Simple telnet server class for handling client connections
 *
 * This class provides a basic telnet server that can accept client connections
 * and handle simple text-based communication.
 */
class TelnetServer {
 public:
  /**
   * @brief Constructor
   * @param port Port number to listen on (default: 23)
   */
  explicit TelnetServer(uint16_t port = 23);

  /**
   * @brief Destructor
   */
  ~TelnetServer();

  /**
   * @brief Start the telnet server
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t start();

  /**
   * @brief Stop the telnet server
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t stop();

  /**
   * @brief Check if server is running
   * @return true if server is running, false otherwise
   */
  bool is_running() const;

  /**
   * @brief Get the port number the server is listening on
   * @return Port number
   */
  uint16_t get_port() const;

  /**
   * @brief Get the number of connected clients
   * @return Number of active client connections
   */
  size_t get_client_count() const;

  // Prevent copying and assignment
  TelnetServer(const TelnetServer&) = delete;
  TelnetServer& operator=(const TelnetServer&) = delete;

 private:
  static constexpr const char* TAG = "telnet_server";
  static constexpr const size_t MAX_CLIENTS = 4;
  static constexpr const size_t BUFFER_SIZE = 256;
  static constexpr const size_t TASK_STACK_SIZE = 4096;
  static constexpr const uint32_t TASK_PRIORITY = 5;

  struct ClientInfo {
    int socket_fd;
    TaskHandle_t task_handle;
    std::string client_ip;
    bool active;
    bool usb_forwarding_mode;  // USB forwarding mode flag
    TelnetServer* server;  // Add server pointer
  };

  uint16_t port_;
  std::atomic<bool> running_;
  std::atomic<size_t> client_count_;
  int server_socket_;
  TaskHandle_t server_task_handle_;
  std::vector<ClientInfo> clients_;

  /**
   * @brief Main server task that accepts client connections
   * @param pvParameters Task parameters (TelnetServer instance)
   */
  static void server_task(void* pvParameters);

  /**
   * @brief Client handler task for individual client communication
   * @param pvParameters Task parameters (ClientInfo pointer)
   */
  static void client_task(void* pvParameters);

  /**
   * @brief Handle incoming data from a client
   * @param client_info Reference to client information
   * @param input Received command string
   */
  void handle_client_data(ClientInfo& client_info, const std::string& input);

  /**
   * @brief Handle USB data forwarding to client
   * @param client_info Reference to client information
   * @param data USB data to forward
   * @param len Length of data
   */
  void handle_usb_data_forwarding(ClientInfo& client_info, const uint8_t* data, size_t len);

  /**
   * @brief Send response to a client
   * @param client_info Reference to client information
   * @param message Message to send
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t send_response(ClientInfo& client_info, const std::string& message);


  /**
   * @brief Find available client slot
   * @return Index of available slot, or -1 if none available
   */
  int find_available_client_slot();

  /**
   * @brief Remove client from active clients list
   * @param client_index Index of client to remove
   */
  void remove_client(int client_index);
};

/**
 * @brief Launch telnet server with default settings
 * @param port Port number to listen on (default: 23)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t launch(uint16_t port = 23);

/**
 * @brief Stop the telnet server
 * @return ESP_OK on success, error code on failure
 */
esp_err_t stop();

/**
 * @brief Check if telnet server is running
 * @return true if server is running, false otherwise
 */
bool is_running();

/**
 * @brief Get the number of connected clients
 * @return Number of active client connections
 */
size_t get_client_count();

}  // namespace telnet_server
