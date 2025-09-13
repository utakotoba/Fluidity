#include "usb_cdc_manager.hpp"
#include <cassert>
#include <cstring>
#include <sstream>
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

namespace usb_cdc_manager {

// Global manager instance
static std::unique_ptr<UsbCdcManager> g_manager_instance = nullptr;

UsbCdcManager::UsbCdcManager()
    : initialized_(false),
      device_open_(false),
      cdc_device_(nullptr),
      usb_lib_task_handle_(nullptr),
      device_disconnected_sem_(nullptr) {}

UsbCdcManager::~UsbCdcManager() {
  deinit();
}

esp_err_t UsbCdcManager::init() {
  if (initialized_.load()) {
    ESP_LOGW(TAG, "USB CDC manager already initialized");
    return ESP_OK;
  }

  // Create semaphore for device disconnection
  device_disconnected_sem_ = xSemaphoreCreateBinary();
  if (!device_disconnected_sem_) {
    ESP_LOGE(TAG, "Failed to create device disconnected semaphore");
    return ESP_ERR_NO_MEM;
  }

  // Install USB Host driver
  ESP_LOGI(TAG, "Installing USB Host");
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .root_port_unpowered = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
      .enum_filter_cb = NULL,
      .fifo_settings_custom = NULL,
  };
  esp_err_t ret = usb_host_install(&host_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(ret));
    vSemaphoreDelete(device_disconnected_sem_);
    device_disconnected_sem_ = nullptr;
    return ret;
  }

  // Create USB library task
  BaseType_t task_created =
      xTaskCreate(usb_lib_task, "usb_lib", USB_LIB_TASK_STACK_SIZE, this,
                  USB_HOST_PRIORITY, &usb_lib_task_handle_);
  if (task_created != pdTRUE) {
    ESP_LOGE(TAG, "Failed to create USB library task");
    usb_host_uninstall();
    vSemaphoreDelete(device_disconnected_sem_);
    device_disconnected_sem_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  // Install CDC-ACM driver
  ESP_LOGI(TAG, "Installing CDC-ACM driver");
  ret = cdc_acm_host_install(NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install CDC-ACM driver: %s", esp_err_to_name(ret));
    vTaskDelete(usb_lib_task_handle_);
    usb_lib_task_handle_ = nullptr;
    usb_host_uninstall();
    vSemaphoreDelete(device_disconnected_sem_);
    device_disconnected_sem_ = nullptr;
    return ret;
  }

  initialized_.store(true);
  ESP_LOGI(TAG, "USB CDC manager initialized successfully");
  return ESP_OK;
}

esp_err_t UsbCdcManager::deinit() {
  if (!initialized_.load()) {
    return ESP_OK;
  }

  // Close any open device
  close_device();

  // Delete USB library task
  if (usb_lib_task_handle_) {
    vTaskDelete(usb_lib_task_handle_);
    usb_lib_task_handle_ = nullptr;
  }

  // Uninstall CDC-ACM driver
  cdc_acm_host_uninstall();

  // Uninstall USB host
  usb_host_uninstall();

  // Delete semaphore
  if (device_disconnected_sem_) {
    vSemaphoreDelete(device_disconnected_sem_);
    device_disconnected_sem_ = nullptr;
  }

  initialized_.store(false);
  ESP_LOGI(TAG, "USB CDC manager deinitialized");
  return ESP_OK;
}

bool UsbCdcManager::is_initialized() const {
  return initialized_.load();
}

esp_err_t UsbCdcManager::open_device(uint16_t vid,
                                     uint16_t pid,
                                     uint8_t instance) {
  if (!initialized_.load()) {
    ESP_LOGE(TAG, "Manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (device_open_.load()) {
    ESP_LOGW(TAG, "Device already open, closing first");
    close_device();
  }

  const cdc_acm_host_device_config_t dev_config = {
      .connection_timeout_ms = 1000,
      .out_buffer_size = BUFFER_SIZE,
      .in_buffer_size = BUFFER_SIZE,
      .event_cb = handle_event,
      .data_cb = handle_rx,
      .user_arg = this,
  };

  ESP_LOGI(TAG, "Opening CDC ACM device 0x%04X:0x%04X (instance %d)...", vid,
           pid, instance);

  esp_err_t ret =
      cdc_acm_host_open(vid, pid, instance, &dev_config, &cdc_device_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open CDC ACM device: %s", esp_err_to_name(ret));
    return ret;
  }

  device_open_.store(true);
  ESP_LOGI(TAG, "CDC ACM device opened successfully");

  // Print device description
  cdc_acm_host_desc_print(cdc_device_);

  return ESP_OK;
}

esp_err_t UsbCdcManager::close_device() {
  if (!device_open_.load()) {
    return ESP_OK;
  }

  if (cdc_device_) {
    esp_err_t ret = cdc_acm_host_close(cdc_device_);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to close CDC ACM device: %s", esp_err_to_name(ret));
    }
    cdc_device_ = nullptr;
  }

  device_open_.store(false);
  ESP_LOGI(TAG, "CDC ACM device closed");
  return ESP_OK;
}

bool UsbCdcManager::is_device_open() const {
  return device_open_.load();
}

esp_err_t UsbCdcManager::send_data(const uint8_t* data,
                                   size_t len,
                                   uint32_t timeout_ms) {
  if (!device_open_.load() || !cdc_device_) {
    ESP_LOGE(TAG, "No device open");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret =
      cdc_acm_host_data_tx_blocking(cdc_device_, data, len, timeout_ms);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send data: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

void UsbCdcManager::set_data_callback(
    std::function<void(const uint8_t*, size_t)> callback) {
  data_callback_ = callback;
}

std::string UsbCdcManager::get_device_info() const {
  if (!device_open_.load()) {
    return "No device connected";
  }

  return "USB CDC ACM device connected";
}

std::string UsbCdcManager::scan_devices() {
  if (!initialized_.load()) {
    return "USB CDC manager not initialized";
  }

  std::stringstream result;
  result << "Scanning for CDC ACM devices...\r\n\r\n";

  // Get list of connected USB devices
  uint8_t dev_addr_list[10];
  int num_of_devices;
  esp_err_t ret = usb_host_device_addr_list_fill(
      sizeof(dev_addr_list), dev_addr_list, &num_of_devices);

  if (ret != ESP_OK) {
    result << "Failed to enumerate USB devices: " << esp_err_to_name(ret)
           << "\r\n";
    return result.str();
  }

  if (num_of_devices == 0) {
    result << "No USB devices found.\r\n";
    return result.str();
  }

  // Create a temporary USB host client for device enumeration
  usb_host_client_handle_t client_hdl;
  const usb_host_client_config_t client_config = {
      .is_synchronous = false,
      .max_num_event_msg = 5,
      .async = {.client_event_callback = nullptr, .callback_arg = nullptr}};

  ret = usb_host_client_register(&client_config, &client_hdl);
  if (ret != ESP_OK) {
    result << "Failed to register USB host client: " << esp_err_to_name(ret)
           << "\r\n";
    return result.str();
  }

  bool found_cdc_acm = false;

  // Check each device for CDC ACM compatibility
  for (int i = 0; i < num_of_devices; i++) {
    usb_device_handle_t device_hdl;
    ret = usb_host_device_open(client_hdl, dev_addr_list[i], &device_hdl);
    if (ret != ESP_OK) {
      continue;
    }

    // Get device descriptor
    const usb_device_desc_t* device_desc;
    ret = usb_host_get_device_descriptor(device_hdl, &device_desc);
    if (ret != ESP_OK) {
      usb_host_device_close(client_hdl, device_hdl);
      continue;
    }

    // Get configuration descriptor
    const usb_config_desc_t* config_desc;
    ret = usb_host_get_active_config_descriptor(device_hdl, &config_desc);
    if (ret != ESP_OK) {
      usb_host_device_close(client_hdl, device_hdl);
      continue;
    }

    // Check if it's a CDC ACM device
    bool is_cdc_acm = false;
    const usb_standard_desc_t* desc = (const usb_standard_desc_t*)config_desc;
    const uint8_t* desc_end =
        (const uint8_t*)config_desc + config_desc->wTotalLength;

    while (desc < (const usb_standard_desc_t*)desc_end) {
      if (desc->bLength == 0) {
        break;
      }

      if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
        const usb_intf_desc_t* intf_desc = (const usb_intf_desc_t*)desc;
        if (intf_desc->bInterfaceClass == USB_CLASS_COMM &&
            intf_desc->bInterfaceSubClass == USB_CDC_SUBCLASS_ACM) {
          is_cdc_acm = true;
          break;
        }
      }

      desc = (const usb_standard_desc_t*)((const uint8_t*)desc + desc->bLength);
    }

    if (is_cdc_acm) {
      result << "Found CDC ACM device:\r\n";
      result << "  VID: 0x" << std::hex << device_desc->idVendor << std::dec
             << "\r\n";
      result << "  PID: 0x" << std::hex << device_desc->idProduct << std::dec
             << "\r\n";
      result << "  Device Address: " << (int)dev_addr_list[i] << "\r\n";
      result << "  Status: Ready for connection\r\n\r\n";
      found_cdc_acm = true;
    }

    usb_host_device_close(client_hdl, device_hdl);
  }

  // Unregister the client
  usb_host_client_deregister(client_hdl);

  if (!found_cdc_acm) {
    result << "No CDC ACM devices found.\r\n";
    result << "Make sure a USB-to-serial device is connected.\r\n";
  }

  return result.str();
}

std::string UsbCdcManager::list_all_usb_devices() {
  if (!initialized_.load()) {
    return "USB CDC manager not initialized";
  }

  std::stringstream result;
  result << "Listing all USB devices...\r\n\r\n";

  // Get list of connected USB devices
  uint8_t dev_addr_list[10];
  int num_of_devices;
  esp_err_t ret = usb_host_device_addr_list_fill(
      sizeof(dev_addr_list), dev_addr_list, &num_of_devices);

  if (ret != ESP_OK) {
    result << "Failed to enumerate USB devices: " << esp_err_to_name(ret)
           << "\r\n";
    return result.str();
  }

  if (num_of_devices == 0) {
    result << "No USB devices found.\r\n";
    return result.str();
  }

  result << "Found " << num_of_devices << " USB device(s):\r\n\r\n";

  // Create a temporary USB host client for device enumeration
  usb_host_client_handle_t client_hdl;
  const usb_host_client_config_t client_config = {
      .is_synchronous = false,
      .max_num_event_msg = 5,
      .async = {.client_event_callback = nullptr, .callback_arg = nullptr}};

  ret = usb_host_client_register(&client_config, &client_hdl);
  if (ret != ESP_OK) {
    result << "Failed to register USB host client: " << esp_err_to_name(ret)
           << "\r\n";
    return result.str();
  }

  // Enumerate each device
  for (int i = 0; i < num_of_devices; i++) {
    usb_device_handle_t device_hdl;
    ret = usb_host_device_open(client_hdl, dev_addr_list[i], &device_hdl);
    if (ret != ESP_OK) {
      result << "Device " << (int)dev_addr_list[i] << ": Failed to open ("
             << esp_err_to_name(ret) << ")\r\n";
      continue;
    }

    // Get device descriptor
    const usb_device_desc_t* device_desc;
    ret = usb_host_get_device_descriptor(device_hdl, &device_desc);
    if (ret != ESP_OK) {
      result << "Device " << (int)dev_addr_list[i]
             << ": Failed to get descriptor (" << esp_err_to_name(ret)
             << ")\r\n";
      usb_host_device_close(client_hdl, device_hdl);
      continue;
    }

    // Get configuration descriptor
    const usb_config_desc_t* config_desc;
    ret = usb_host_get_active_config_descriptor(device_hdl, &config_desc);
    if (ret != ESP_OK) {
      result << "Device " << (int)dev_addr_list[i]
             << ": Failed to get config descriptor (" << esp_err_to_name(ret)
             << ")\r\n";
      usb_host_device_close(client_hdl, device_hdl);
      continue;
    }

    // Print device information
    result << "Device " << (int)dev_addr_list[i] << ":\r\n";
    result << "  VID: 0x" << std::hex << device_desc->idVendor << std::dec
           << "\r\n";
    result << "  PID: 0x" << std::hex << device_desc->idProduct << std::dec
           << "\r\n";
    result << "  Class: 0x" << std::hex << (int)device_desc->bDeviceClass
           << std::dec << "\r\n";
    result << "  Subclass: 0x" << std::hex << (int)device_desc->bDeviceSubClass
           << std::dec << "\r\n";
    result << "  Protocol: 0x" << std::hex << (int)device_desc->bDeviceProtocol
           << std::dec << "\r\n";
    result << "  Manufacturer: " << (int)device_desc->iManufacturer << "\r\n";
    result << "  Product: " << (int)device_desc->iProduct << "\r\n";
    result << "  Serial: " << (int)device_desc->iSerialNumber << "\r\n";

    // Check if it's a CDC ACM device
    bool is_cdc_acm = false;
    const usb_standard_desc_t* desc = (const usb_standard_desc_t*)config_desc;
    const uint8_t* desc_end =
        (const uint8_t*)config_desc + config_desc->wTotalLength;

    while (desc < (const usb_standard_desc_t*)desc_end) {
      if (desc->bLength == 0) {
        break;
      }

      if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
        const usb_intf_desc_t* intf_desc = (const usb_intf_desc_t*)desc;
        if (intf_desc->bInterfaceClass == USB_CLASS_COMM &&
            intf_desc->bInterfaceSubClass == USB_CDC_SUBCLASS_ACM) {
          is_cdc_acm = true;
          result << "  Interface " << (int)intf_desc->bInterfaceNumber
                 << ": CDC ACM\r\n";
        }
      }

      desc = (const usb_standard_desc_t*)((const uint8_t*)desc + desc->bLength);
    }

    if (is_cdc_acm) {
      result << "  Status: CDC ACM compatible\r\n";
    } else {
      result << "  Status: Not CDC ACM compatible\r\n";
    }

    result << "\r\n";

    usb_host_device_close(client_hdl, device_hdl);
  }

  // Unregister the client
  usb_host_client_deregister(client_hdl);

  return result.str();
}

void UsbCdcManager::usb_lib_task(void* arg) {
  UsbCdcManager* manager = static_cast<UsbCdcManager*>(arg);

  ESP_LOGI(TAG, "USB library task started");

  while (manager->initialized_.load()) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_ERROR_CHECK(usb_host_device_free_all());
    }

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "USB: All devices freed");
    }
  }

  ESP_LOGI(TAG, "USB library task ended");
  vTaskDelete(nullptr);
}

bool UsbCdcManager::handle_rx(const uint8_t* data, size_t data_len, void* arg) {
  UsbCdcManager* manager = static_cast<UsbCdcManager*>(arg);

  ESP_LOGI(TAG, "Data received from USB device (%zu bytes)", data_len);
  ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_DEBUG);

  // Call the data callback if set
  if (manager->data_callback_) {
    manager->data_callback_(data, data_len);
  }

  return true;
}

void UsbCdcManager::handle_event(const cdc_acm_host_dev_event_data_t* event,
                                 void* user_ctx) {
  UsbCdcManager* manager = static_cast<UsbCdcManager*>(user_ctx);

  switch (event->type) {
    case CDC_ACM_HOST_ERROR:
      ESP_LOGE(TAG, "CDC-ACM error has occurred, err_no = %i",
               event->data.error);
      break;

    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
      ESP_LOGI(TAG, "Device suddenly disconnected");
      if (manager->cdc_device_) {
        cdc_acm_host_close(manager->cdc_device_);
        manager->cdc_device_ = nullptr;
        manager->device_open_.store(false);
      }
      if (manager->device_disconnected_sem_) {
        xSemaphoreGive(manager->device_disconnected_sem_);
      }
      break;

    case CDC_ACM_HOST_SERIAL_STATE:
      ESP_LOGI(TAG, "Serial state notification 0x%04X",
               event->data.serial_state.val);
      break;

    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
      ESP_LOGW(TAG, "Unsupported CDC event: %i", event->type);
      break;
  }
}

// Global functions
UsbCdcManager& get_instance() {
  if (!g_manager_instance) {
    g_manager_instance = std::make_unique<UsbCdcManager>();
  }
  return *g_manager_instance;
}

esp_err_t init() {
  return get_instance().init();
}

esp_err_t deinit() {
  if (!g_manager_instance) {
    return ESP_OK;
  }
  esp_err_t ret = g_manager_instance->deinit();
  g_manager_instance.reset();
  return ret;
}

bool is_initialized() {
  return g_manager_instance && g_manager_instance->is_initialized();
}

esp_err_t open_device(uint16_t vid, uint16_t pid, uint8_t instance) {
  return get_instance().open_device(vid, pid, instance);
}

esp_err_t close_device() {
  return get_instance().close_device();
}

bool is_device_open() {
  return g_manager_instance && g_manager_instance->is_device_open();
}

esp_err_t send_data(const uint8_t* data, size_t len, uint32_t timeout_ms) {
  return get_instance().send_data(data, len, timeout_ms);
}

void set_data_callback(std::function<void(const uint8_t*, size_t)> callback) {
  get_instance().set_data_callback(callback);
}

std::string get_device_info() {
  return g_manager_instance ? g_manager_instance->get_device_info()
                            : "Manager not initialized";
}

std::string scan_devices() {
  return g_manager_instance ? g_manager_instance->scan_devices()
                            : "Manager not initialized";
}

std::string list_all_usb_devices() {
  return g_manager_instance ? g_manager_instance->list_all_usb_devices()
                            : "Manager not initialized";
}

}  // namespace usb_cdc_manager
