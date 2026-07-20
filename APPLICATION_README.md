# STM32F746G-DISCO application

This project combines ThreadX, GUIX 6.1.10, USBX and NetX Duo on the
STM32F746G-DISCO board.

## Implemented functions

- GUIX RGB565 interface at 480 x 272 with STATUS, CONTROLS and NETWORK tabs.
- FT5336 touch input over I2C3.
- BME280 temperature, pressure and humidity over a separate I2C1 bus.
- USBX vendor-specific Bulk device on USB OTG FS.
- WinUSB automatic binding through Microsoft OS 1.0 descriptors.
- NetX Duo IPv4 with DHCP and a single-client TCP server on port 7001.
- Common device state shared safely by the GUI, USB and LAN threads.
- Saturating USB/LAN RX/TX counters and 250 ms activity indicators.
- Per-board Ethernet MAC address and USB serial derived from the STM32 UID.

## USB compatibility

The USB interface keeps the values used by the previous STM32F4 device:

- VID: `0x0483`
- PID: `0x5752`
- Bulk OUT: `0x01`
- Bulk IN: `0x81`
- packet size: 64 bytes
- WinUSB interface GUID: `{8B4B6B6A-3266-4A18-AC3B-7110B602D0A3}`

Each 64-byte OUT packet updates command bytes 0 through 3. Every accepted
packet receives one 64-byte IN response. BME280 values retain the old wire
layout:

- bytes 5..8: signed temperature, little-endian, 0.01 C units
- bytes 9..12: unsigned pressure, little-endian, 0.0001 hPa units
- bytes 13..16: unsigned humidity, little-endian, legacy F4 integer units
  (`%RH x1024`; GUIX converts it to a decimal percentage for display)

Bytes 20..51 contain the optional F746 extension (signature, status flags,
counters, IPv4 address and command snapshot). The old Windows application can
ignore this extension.

## LAN protocol

After DHCP succeeds, connect to the IPv4 address shown on the STATUS tab at
TCP port 7001. TCP is a byte stream, but the application frames it into exact
64-byte messages. The request and response layout is identical to USB.

Only one TCP client is accepted at a time. `CLOSE CLIENT` disconnects it and
returns the server to listening state. `DHCP RENEW` requests lease renewal.

## Sensor integration

Connect the BME280 to the Arduino headers using 3.3 V logic:

- `SCL` to `D15/PB8` (`I2C1_SCL`)
- `SDA` to `D14/PB9` (`I2C1_SDA`)
- `VCC/VIN` to `3V3`
- `GND` to `GND`

The sensor thread probes both 7-bit addresses `0x76` and `0x77`, reads register
`0xD0`, and accepts BME280 chip ID `0x60`. A BMP280 reports `0x58` and is not
accepted because the application packet also requires humidity. HAL receives
`address << 1`; the application never confuses a 7-bit address with the old
F4 shifted values `0xEC/0xEE`.

One forced measurement is made each second. The compensated legacy-compatible
scales are degrees C x100, pascals x100, and relative humidity percent x1024.
`App_State_SetSensor()` immediately publishes each successful sample to GUIX,
USB and LAN. If the device is absent, startup continues and probing is retried
every five seconds.

## Memory layout

- `0x20000000`, 64 KiB DTCM: C heap/stack reserve.
- `0x20010000`, 192 KiB internal SRAM: normal globals and RTOS control data.
- `0x20040000`, 64 KiB non-cacheable SRAM: Ethernet DMA descriptors/data.
- `0xC0000000`, first 512 KiB SDRAM, non-cacheable: LTDC framebuffer.
- `0xC0080000`, remaining SDRAM, cacheable: ThreadX/NetX/USBX memory pools.

The MPU configuration in `main.c` is required. Do not disable it while D-cache
is enabled. NetX packet payloads are aligned to 32-byte cache lines.

## Build and first test

1. Import the project into STM32CubeIDE and build the Debug configuration.
2. Check the linker memory report and confirm `.framebuffer` is in SDRAM and
   `.RxDecripSection`/`.TxDecripSection` are in `ETH_RAM`.
3. Flash the board. The GUI should open on the STATUS tab.
4. Check USART6 for `[BME280] detected and initialized`, address `0x76` or
   `0x77`, chip ID `0x60`, and the first compensated sample.
5. Connect a USB data cable to the `CN13` USB OTG FS Micro-AB connector. Do
   not use `CN12` (USB OTG HS/ULPI) or `CN14` (ST-LINK). The status should
   progress from cable to configured; the application flag becomes active
   after the first valid OUT packet.
6. Connect Ethernet to a DHCP network. Verify LINK, IP and then CLIENT status.
7. Send 64-byte requests by USB and TCP and verify the RX/TX counters and
   short activity marks.

## Startup diagnostics

Early boot diagnostics use USART6 directly, without `printf` or a HAL UART
dependency:

- TX: Arduino `D1`, MCU `PC6`
- RX: Arduino `D0`, MCU `PC7`
- format: `115200 8N1`, 3.3 V logic

Connect the adapter RX input to `D1/PC6` and join the adapter and board grounds.
At reset the firmware tests SDRAM, initializes LTDC and displays red, green,
blue and white vertical bars before ThreadX starts. The bars distinguish the
display/SDRAM path from GUIX. USART output then reports every GUIX, NetX Duo and
USBX initialization result. `[RTOS] alive` is printed every five seconds after
the scheduler starts. Missing BME280, Ethernet cable or USB cable is not a boot
error; missing sensor values remain zero while the I2C1 task retries detection.

Ethernet link and DHCP are serviced dynamically. The cable may be absent at
boot: after it is connected, the firmware enables the MAC and starts DHCP; on
disconnect it stops/reinitializes DHCP so that a later reconnect works.

NetX Duo and USBX are started from separate low-priority ThreadX service
threads. This is intentional: `tx_application_define()` must return before the
scheduler can run, and DHCP is not allowed to start from that pre-scheduler
context. A slow or disconnected optional interface therefore cannot prevent
GUIX, touch input or the heartbeat thread from running. A normal log contains
`[RTOS] tx_application_define complete` followed by `[RTOS] scheduler running`;
the NetX and USBX stage messages may then be interleaved.

CPU faults print `CFSR`, `HFSR`, `MMFAR` and `BFAR` together with the last boot
stage, which makes SDRAM/MPU faults visible without a debugger.

## CubeMX regeneration note

The IOC contains I2C1 on PB8/PB9, I2C3, the MPU, SDRAM timing and
interrupt-priority settings, but
CubeMX does not know about the separately added GUIX library or the custom
USBX vendor class. After any future code regeneration, review `main.c` and
`.cproject`: USB PCD must be initialized by `MX_USBX_Device_Init()` only after
the USBX stack/class is registered, and the GUIX include paths plus
`GX_INCLUDE_USER_DEFINE_FILE` must remain enabled.

GUIX was added from the official Eclipse ThreadX GUIX `v6.1.10_rel` source and
is covered by the license copied to `Middlewares/ST/guix/LICENSE.txt`.
