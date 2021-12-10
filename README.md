# DuetWifiSocketServer

This is a brief description howto setup the build environment for DuetWifiSocketServer

# Setup

## Toolchain

Go to [ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK#get-toolchain),
download and install latest toolchain.

## Repository

Clone repository and checkout submodules.

```
$ git clone git@github.com:Duet3D/DuetWifiSocketServer.git
$ cd DuetWifiSocketServer
$ git submodule init
$ git submodule update --recursive
```

## Configuration

Setup the following environment variables. They are needed for the ESP8266 RTOS SDK.

```
export IDF_PATH=/path/to/lib/esp8266-rtos-sdk
export PATH=/path/to/xtensa-lx106-elf-esp-toolchain/bin:$PATH
```

## Build

To run the build execeute

```
$ cmake -B build
$ make -C build all -j12
```

# Downloads

## Xtensa Toolchain

- https://github.com/espressif/ESP8266_RTOS_SDK#get-toolchain

## Esptool

Esptool is needed to flash and communicate with the ESP8266 chip. It can be found at

- https://github.com/espressif/esptool

### Example

```sh
> esptool.py \
    --chip esp8266 \
    --port /dev/ttyUSB0 \
    --baud 115200 \
    --before default_reset \
    --after hard_reset write_flash \
    -z \
    --flash_mode dio --flash_freq 40m --flash_size 2MB \
    0x0 build/bootloader/bootloader.bin \
    0x10000 build/dwss.bin \
    0x8000 build/partition_table/partition-table.bin
```

## Sources

- https://github.com/Duet3D/DuetWifiSocketServer

## Links

- forum - https://forum.duet3d.com/
- wiki - https://duet3d.dozuki.com/
