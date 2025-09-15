#pragma once

#include "esp_log.h"

namespace usbip {

const uint16_t VERSION = 0x0111;  // USB/IP protocol version 1.1.1

/* ------------------------------------------------------------------------- */
/* Operation Stage (list & import)                                           */
/* ------------------------------------------------------------------------- */

enum op_code : uint16_t {
  OP_REQ_DEVLIST = 0x8005,  // Retrieve the list of exported USB devices
  OP_REP_DEVLIST = 0x0005,  // Reply with the list of exported USB devices
  OP_REQ_IMPORT = 0x8003,   // Request to import (attach) a remote USB device
  OP_REP_IMPORT = 0x0003,   // Reply to import (attach) a remote USB device
};

enum op_status : uint32_t {
  UNUSED = 0x00000000,  // Unused status code (in request)
  OK = 0x00000000,      // OK status (in response)
  ERROR = 0x00000001,   // Error status (in response)
};

enum device_speed : uint32_t {
  UNKNOWN_SPEED = 0,
  LOW_SPEED = 1,
  FULL_SPEED = 2,
};

struct device_interface_t {
  uint8_t interface_class;
  uint8_t interface_subclass;
  uint8_t interface_protocol;
  uint8_t padding;
};

struct op_header_t {
  uint16_t version;
  op_code code;
  op_status status;
};

struct device_descriptor_t {
  char path[256];    // Path of the device on the host exporting the USB device
  char bus_id[32];   // Bus ID of the exported device
  uint32_t bus_num;  // busnum
  uint32_t dev_num;  // devnum
  device_speed speed;           // speed
  uint16_t vendor_id;           // idVendor
  uint16_t product_id;          // idProduct
  uint16_t device_bcd;          // bcdDevice
  uint8_t device_class;         // bDeviceClass
  uint8_t device_subclass;      // bDeviceSubClass
  uint8_t device_protocol;      // bDeviceProtocol
  uint8_t configuration_value;  // bConfigurationValue
  uint8_t configuration_num;    // bNumConfigurations
  uint8_t interface_num;        // bNumInterfaces
};

struct op_req_devlist_t {
  op_header_t header;  // OP_REQ_DEVLIST only contain the header
};

struct op_rep_devlist_t {
  op_header_t header;
  uint32_t exported_count;           // Number of exported devices
  device_descriptor_t descriptor;    // Device descriptor
  device_interface_t interfaces[4];  // Device interfaces (limited to 4 now)
};

struct op_req_import_t {
  op_header_t header;
  char bus_id[32];  // Bus ID of the exported device on the remote host
};

struct op_rep_import_t {
  op_header_t header;
  device_descriptor_t descriptor;
};

/* ------------------------------------------------------------------------- */
/* Transmission Stage (urb traffic)                                          */
/* ------------------------------------------------------------------------- */

enum xfer_command_t : uint32_t {
  CMD_SUBMIT = 0x00000001,  // USBIP_CMD_SUBMIT
  RET_SUBMIT = 0x00000003,  // USBIP_RET_SUBMIT
  CMD_UNLINK = 0x00000002,  // USBIP_CMD_UNLINK
  RET_UNLINK = 0x00000004,  // USBIP_RET_UNLINK
};

enum xfer_direction_t : uint32_t {
  OUT = 0,  // USBIP_DIR_OUT
  IN = 1,   // USBIP_DIR_IN
};

struct xfer_header_t {
  xfer_command_t command;
  uint32_t seq_num;            // Sequential number that identifies reqs & reps
  uint32_t device_id;          // Specifies a remote USB device uniquely
  xfer_direction_t direction;  // Transmission direction
  uint32_t endpoint;           // Endpoint number
};

// check the definition in Linux kernel
// https://github.com/torvalds/linux/blob/master/drivers/usb/usbip/usbip_common.h
struct iso_packet_descriptor_t {
  uint32_t offset;
  uint32_t length;
  uint32_t actual_length;
  uint32_t status;
};

struct cmd_submit_t {
  xfer_header_t header;
  uint32_t transfer_flags;          // Depend on the USBIP_URB transfer_flags
  uint32_t transfer_buffer_length;  // Use URB transfer_buffer_length
  uint32_t start_frame;             // Use URB start_frame
  uint32_t number_of_packets;       // Number of ISO packets
  uint32_t interval;  // Max time for the request on the server host controller
  uint64_t setup;     // Data bytes for USB setup, filled with zeros if not used
  uint8_t payload[];  // Transmission buffer + ISO packet descriptor
};

struct ret_submit_t {
  xfer_header_t header;
  uint32_t status;             // URB transaction status
  uint32_t actual_length;      // Use URB actual_length
  uint32_t start_frame;        // Use URB start_frame
  uint32_t number_of_packets;  // Number of ISO packets
  uint32_t error_count;        // Transaction error count
  uint64_t padding;            // Padding, shall be set to 0
  uint8_t payload[];           // Transmission buffer + ISO packet descriptor
};

struct cmd_unlink_t {
  xfer_header_t header;
  uint32_t unlink_seqnum;  // The SUBMIT request to unlink
  uint8_t padding[24];     // Padding, shall be set to 0
};

struct ret_unlink_t {
  xfer_header_t header;
  uint32_t status;      // URB unlink status
  uint8_t padding[24];  // Padding, shall be set to 0
};

}  // namespace usbip
