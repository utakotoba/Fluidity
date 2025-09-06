### Fluidity

The Fluidity project is a small hardware device that shares connected USB endpoints over a network via the TCP protocol, leveraging the USB/IP protocol in the Linux kernel.

### Progress

> Still in early development.

Currently, an ESP32-S3 chip has been selected as the main MCU to handle network traffic and protocol conversion. This choice is based on an [existing project](https://github.com/chegewara/esp32-usbip-poc) that provides a proof of concept on this chip. Although it comes with some limitations, it serves as a solid starting point for further improvement. The work is currently based on the latest stable release of [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/index.html).

However, during early local network testing, the chip often failed to handle high-frequency URB requests, such as flashing another chip remotely. In most cases, it could not keep up due to a lack of optimization—or possibly hardware constraints—since the chip does not feature USB DMA, requiring the CPU to copy data every time. For extensibility, this project may be shifted to the STM32H7 series, which offers higher computational performance, USB buffering and DMA support, and broader compatibility with USB interfaces.
