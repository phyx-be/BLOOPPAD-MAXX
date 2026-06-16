# BLOOPAD-MAXX
an illuminated 8x8 keypad with a cheap USB microcontroller.

This firmware runs on the [CH32X035](https://www.wch-ic.com/products/CH32X035.html) MCU of the BloopPad Maxx.

The firmware handles different functions:
 * Read the button matrix
 * Set the button RGB LEDs
 * Send MIDI CC messages over USB to a PC and over UART
 * Allow control from other devices using I2C (Caution: BloopPad runs at 5V!)

Please check the [schematics](TODO:local_link) to get more details about how these peripherals are connected to the CH32X035 MCU.

### UART

The BloopPad Maxx sends MIDI CC messages through the UART port at a baudrate of `115200 8N1`.

TODO: receive MIDI CC messages to control the LEDs.

### I2C

The BloopPad Maxx has I2C address `0x55` and uses the following registers to interface/control with its connected peripherals:

| Address (hex) | Name | Access | Bytes | description |
|-|-|-|-|-|
| 0x00 | Version number | R | 3 | Reports the firmware version number |
| 0x03 | Button states | R | 8 | Reports the button states |
| 0x0b | Button RGB LEDs | R/W | 192 | The RGB value of the button leds |

## Building

Use [platformio](https://platformio.org) to build this project. You should install the [ch32v platform package](https://github.com/Community-PIO-CH32V/platform-ch32v) as well. Follow [these instructions](https://pio-ch32v.readthedocs.io/en/latest/installation.html) to do so. If you use the command line, build using:

```
pio run
```

## Flashing

The easiest way to flash the BloopPad is using the USB port and a tool like [wchisp](https://github.com/ch32-rs/wchisp). First, disconnect the USB cable. While pressing the boot button on the board, reconnect the USB cable. Then run:

```
wchisp flash <path to the firmware.bin file>
```

Or using Platformio:

```
pio run -t upload
```
