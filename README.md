### Fluidity

This hardware project is designed for remote debugging and flashing in embedded development. It provides broad tool compatibility along with a set of clean yet feature-rich built-in tools.

### Architecture

The project is planned to support three types of connection methods.  
1. A custom-patched OpenOCD server that communicates with the remote debugger using a dedicated protocol (such as [openocd-elaphurelink](https://github.com/windowsair/openocd-elaphurelink)).  
2. An [RFC2217](https://www.rfc-editor.org/rfc/rfc2217.html) server that exposes the connected serial port.  
3. A [USB/IP](https://docs.kernel.org/usb/usbip_protocol.html) server as a fallback option.  

The first two options work on most systems, while the USB/IP server is natively supported only on Linux kernels and Windows.

### Notice

This project currently runs on the ESP32-S3 N16R8, so the SDK configuration is tailored to that storage specification.

It is built with the experimental Clang toolchain instead of GCC. Under `-O2` optimization, compilation errors may occur in the file `$IDF_PATH/components/mbedtls/port/esp_ds/esp_rsa_dec_alt.c`. Until an official fix is released, you may need to manually apply this [PR](https://github.com/espressif/esp-idf/pull/17582).
