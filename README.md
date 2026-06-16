# BLOOPAD-MAXX
an illuminated 8x8 keypad with a cheap USB microcontroller.

This firmware runs on the [CH32X035](https://www.wch-ic.com/products/CH32X035.html) MCU of the BloopPad Maxx.

The firmware handles different functions:
 * Read the button matrix
 * Set the button RGB LEDs
 * Send MIDI CC messages over USB to a PC and over UART
 * Allow control from other devices using I2C (Caution: BloopPad runs at 5V!)

Please check the [schematics](TODO:local_link) to get more details about how these peripherals are connected to the CH32X035 MCU.

### MIDI messages

The BloopPad Maxx sends and receives MIDI CC messages through USB or UART (baudrate of `115200 8N1`) to receive the button state or control the LEDs.

The MIDI CC (Control Change) messages related to the buttons are sent every time the state of one of the buttons changes. For each button, the value in the MIDI CC message is `0x7F` when the button is pressed and `0x00` when it is not pressed. Each button has the following CC number assigned:

|  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-|-|-|-|-|-|-|-|-|
| 0 | 0x00 | 0x01 | 0x02 | 0x03 | 0x04 | 0x05 | 0x06 | 0x07 |
| 1 | 0x10 | 0x11 | 0x12 | 0x13 | 0x14 | 0x15 | 0x16 | 0x17 |
| 2 | 0x20 | 0x21 | 0x22 | 0x23 | 0x24 | 0x25 | 0x26 | 0x27 |
| 3 | 0x30 | 0x31 | 0x32 | 0x33 | 0x34 | 0x35 | 0x36 | 0x37 |
| 4 | 0x40 | 0x41 | 0x42 | 0x43 | 0x44 | 0x45 | 0x46 | 0x47 |
| 5 | 0x50 | 0x51 | 0x52 | 0x53 | 0x54 | 0x55 | 0x56 | 0x57 |
| 6 | 0x60 | 0x61 | 0x62 | 0x63 | 0x64 | 0x65 | 0x66 | 0x67 |
| 7 | 0x70 | 0x71 | 0x72 | 0x73 | 0x74 | 0x75 | 0x76 | 0x77 |

De LEDs beneath the buttons can be controlled by sending a MIDI CC message with a value between 0x01 and 0x0A, which determines the color. Each LED has the following CC number assigned:

|  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-|-|-|-|-|-|-|-|-|
| 0 | 0x08 | 0x09 | 0x0A | 0x0B | 0x0C | 0x0D | 0x0E | 0x0F |
| 1 | 0x18 | 0x19 | 0x1A | 0x1B | 0x1C | 0x1D | 0x1E | 0x1F |
| 2 | 0x28 | 0x29 | 0x2A | 0x2B | 0x2C | 0x2D | 0x2E | 0x2F |
| 3 | 0x38 | 0x39 | 0x3A | 0x3B | 0x3C | 0x3D | 0x3E | 0x3F |
| 4 | 0x48 | 0x49 | 0x4A | 0x4B | 0x4C | 0x4D | 0x4E | 0x4F |
| 5 | 0x58 | 0x59 | 0x5A | 0x5B | 0x5C | 0x5D | 0x5E | 0x5F |
| 6 | 0x68 | 0x69 | 0x6A | 0x6B | 0x6C | 0x6D | 0x6E | 0x6F |
| 7 | 0x78 | 0x79 | 0x7A | 0x7B | 0x7C | 0x7D | 0x7E | 0x7F |

This is the mapping between the value and the RGB color of the LED:

| Value | Red | Green | Blue | Name |
|-|-|-|-|-|
| 0x00 | 0x00 | 0x00 | 0x00 | LED off |
| 0x01 | 0xc5 | 0x0a | 0x08 | orange-red |
| 0x02 | 0x32 | 0xbe | 0x44 | teal |
| 0x03 | 0x42 | 0xd4 | 0xf4 | yellow-green |
| 0x04 | 0xf8 | 0xd2 | 0x00 | warm white |
| 0x05 | 0x00 | 0x44 | 0xff | blue |
| 0x06 | 0xaf | 0x00 | 0xcc | cyan |
| 0x07 | 0xfc | 0xa6 | 0xd7 | white |
| 0x08 | 0xf2 | 0xf2 | 0xff | bright white |
| 0x09 | 0xff | 0x80 | 0x00 | green |

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
pio run -e debug
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
