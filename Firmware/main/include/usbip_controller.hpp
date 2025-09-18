#pragma once

#include <vector>
#include "usbip_defs.hpp"

namespace usbip {

using device_id_t = uint32_t;

class Device {
 public:
  virtual ~Device() = default;

  virtual device_descriptor_t get_descriptor() const = 0;

  virtual std::vector<device_interface_t> get_interfaces() const = 0;

  virtual std::pair<const uint8_t*, size_t> submit_urb(const cmd_submit_t& cmd,
                                                       const uint8_t* data,
                                                       size_t length) = 0;

  virtual bool unlink_urb(const cmd_unlink_t& cmd) = 0;

 protected:
  Device() = default;
};

class Provider {
 public:
};

}  // namespace usbip
