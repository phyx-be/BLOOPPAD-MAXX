/* USB-MIDI controller mode: buttons are sent as MIDI Control Change messages,
 * the host sets the LEDs with Control Change (palette colors) or SysEx (RGB) messages
 */
#include <string.h> /* memcpy(), memcmp() */

#include <wch_usbmidi_internal.h>

#include "debug.h"
#include "games.h"

/* midi-usb */
#define MIDI_CHANNEL (0)
#define MIDI_MAX     (0x7f)

/* USB-MIDI Code Index Numbers (CIN), USB MIDI spec table 4-1 */
#define MIDI_CIN_SYSEX_START_CONT  (0x04) /* SysEx starts or continues */
#define MIDI_CIN_SYSEX_END_1BYTE   (0x05) /* SysEx ends with following single byte, or 1-byte System Common */
#define MIDI_CIN_SYSEX_END_2BYTE   (0x06) /* SysEx ends with following two bytes, or empty SysEx */
#define MIDI_CIN_SYSEX_END_3BYTE   (0x07) /* SysEx ends with following three bytes */
#define MIDI_CIN_NOTE_OFF          (0x08)
#define MIDI_CIN_NOTE_ON           (0x09)
#define MIDI_CIN_POLY_KEY_PRESSURE (0x0A)
#define MIDI_CIN_CONTROL_CHANGE    (0x0B)
#define MIDI_CIN_PROGRAM_CHANGE    (0x0C)
#define MIDI_CIN_CHANNEL_PRESSURE  (0x0D)
#define MIDI_CIN_PITCH_BEND        (0x0E)
#define MIDI_CIN_SINGLE_BYTE       (0x0F) /* System Real-Time */
#define MIDI_CIN_MASK              (0x0F)

/* MIDI channel voice/system status bytes */
#define MIDI_STATUS_CONTROL_CHANGE (0xB0)
#define MIDI_STATUS_BIT            (0x80) /* set on every status byte, clear on every data byte */
#define MIDI_CHANNEL_MASK          (0x0F)
#define MIDI_SYSEX_START           (0xF0)
#define MIDI_SYSEX_END             (0xF7)
#define MIDI_TUNE_REQUEST          (0xF6)

/* SysEx manufacturer ID bytes identifying our custom BloopPad Maxx LED protocol */
#define SYSEX_MANUFACTURER_ID_1 (0x13)
#define SYSEX_MANUFACTURER_ID_2 (0x37)

/* Predefined color palette used when the host sends a CC value 1–9 to set an LED.
 * Value 0 turns the LED off.
 */
static const game_color_t mixxx_palette[] = {
    {.g = 0x00, .r = 0x00, .b = 0x00}, /* 0: led off */
    {.g = 0x0a, .r = 0xc5, .b = 0x08}, /* 1: orange-red */
    {.g = 0xbe, .r = 0x32, .b = 0x44}, /* 2: teal */
    {.g = 0xd4, .r = 0x42, .b = 0xf4}, /* 3: yellow-green */
    {.g = 0xd2, .r = 0xf8, .b = 0x00}, /* 4: warm white */
    {.g = 0x44, .r = 0x00, .b = 0xff}, /* 5: blue */
    {.g = 0x00, .r = 0xaf, .b = 0xcc}, /* 6: cyan */
    {.g = 0xa6, .r = 0xfc, .b = 0xd7}, /* 7: white */
    {.g = 0xf2, .r = 0xf2, .b = 0xff}, /* 8: bright white */
    {.g = 0x80, .r = 0xff, .b = 0x00}, /* 9: green */
};

/* flag to indicate that the LEDs have to be updated with a new value from USB-MIDI */
static uint8_t flag_update_leds = 0;

/* variables for MIDI SysEx processing */
#define MAX_SYSEX_DATA ((4 * GAME_ROWS * GAME_COLS) + 4)
static uint8_t in_sysex = 0;
static uint8_t sysex_data[MAX_SYSEX_DATA];
static int sysex_data_len = 0;

/* send a USB packet */
static void USBSendPacket(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t packet[4];
    packet[0] = (cin & MIDI_CIN_MASK); /* Cable 0 */
    packet[1] = b1;
    packet[2] = b2;
    packet[3] = b3;
    USB_write(packet, 4);
}

/* Send a MIDI Control Change message over USB.
 * Used to report button press/release: control = CC# encoding (row, col),
 * value = MIDI_MAX (0x7f) on press, 0 on release. */
static void USBSendControlChange(uint8_t channel, uint8_t control, uint8_t value)
{
    USBSendPacket(MIDI_CIN_CONTROL_CHANGE, MIDI_STATUS_CONTROL_CHANGE | (channel & MIDI_CHANNEL_MASK), control, value);
}

static void finalize_sysex(void)
{
    if (sysex_data_len < 8 || (sysex_data_len % 4) != 0)
    {
        PRINT("Unsupported SysEx size: %d\r\n", sysex_data_len);
        goto out;
    }

    if (sysex_data[1] != SYSEX_MANUFACTURER_ID_1 || sysex_data[2] != SYSEX_MANUFACTURER_ID_2)
    {
        PRINT("SysEx: Unsupported manufacturing ID: 0x%02X%02X\r\n", sysex_data[1], sysex_data[2]);
        goto out;
    }

    for (int i = 3; (i + 4) < sysex_data_len && sysex_data[i] != MIDI_SYSEX_END; i += 4)
    {

        uint8_t row = sysex_data[i] >> 4;
        uint8_t col = sysex_data[i] & 0x0F;

        if (row >= GAME_ROWS)
        {
            /* invalid row index */
            PRINT("SysEx: invalid row index sysex_data[%d] 0x%x\r\n", i, sysex_data[i]);
            continue;
        }

        if (col < 0x08)
        {
            /* col must be 0x08–0x0F for LED addressing (low nibble 0–7 is button-event space) */
            PRINT("SysEx: invalid column index sysex_data[%d] 0x%x\r\n", i, sysex_data[i]);
            continue;
        }

        /* SysEx data bytes are 7-bit (0-127);
         * shift left to scale into the 8-bit 0-254 color range
         */
        board_set_led(row, col - 0x08, sysex_data[i + 1] << 1, sysex_data[i + 2] << 1, sysex_data[i + 3] << 1);
        flag_update_leds = 1;
    }

out:
    in_sysex = 0;
    sysex_data_len = 0;
}

static void handle_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t channel = b1 & MIDI_CHANNEL_MASK;
    uint8_t row;
    uint8_t col;

    switch (cin)
    {
        case MIDI_CIN_NOTE_OFF:
            PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case MIDI_CIN_NOTE_ON:
            if (b3 > 0)
            {
                PRINT("note on: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            else if (b3 == 0)
            {
                PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            break;

        case MIDI_CIN_POLY_KEY_PRESSURE:
            PRINT("Poly key pressure: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case MIDI_CIN_CONTROL_CHANGE:
            if (channel != MIDI_CHANNEL)
            {
                PRINT("we ignore channel %d\r\n", channel);
                break;
            }

            row = b2 >> 4;
            col = b2 & 0x0F;

            if (row >= GAME_ROWS)
            {
                /* invalid row index */
                PRINT("Control Change: invalid row index b2 0x%x\r\n", b2);
                break;
            }

            if (col < 0x08)
            {
                /* low nibble 0–7: this is a button-event CC#, not an LED command */
                PRINT("Control Change: invalid column index b2 0x%x\r\n", b2);
                break;
            }

            if (b3 < (sizeof(mixxx_palette) / sizeof(mixxx_palette[0])))
            {
                game_set_led(row, col - 0x08, mixxx_palette[b3]);
                flag_update_leds = 1;
            }
            else
            {
                PRINT("Control Change: invalid color index b3 0x%x\r\n", b3);
            }
            break;

        case MIDI_CIN_PROGRAM_CHANGE:
            PRINT("Program change: channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case MIDI_CIN_CHANNEL_PRESSURE:
            PRINT("Channel Pressure (Aftertouch): channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case MIDI_CIN_PITCH_BEND:
            /* Reconstruct 14-bit value from LSB (b2) and MSB (b3) */
            int val = (b2 & 0x7F) | ((b3 & 0x7F) << 7);
            val -= 8192; /* Center at 0 */
            PRINT("Pitch bend: channel %d, val %d\r\n", channel, val);
            break;

        case MIDI_CIN_SINGLE_BYTE:
            PRINT("single byte: 0x%02x\r\n", b1);
            break;

        case MIDI_CIN_SYSEX_START_CONT:

            /* SysEx start */
            if (b1 == MIDI_SYSEX_START)
            {
                sysex_data_len = 0;
                in_sysex = 1;
            }

            /* SysEx continues — but only if b1 is not a status byte */
            if (((in_sysex && !(b1 & MIDI_STATUS_BIT)) || b1 == MIDI_SYSEX_START) && sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
            }

            break;

        case MIDI_CIN_SYSEX_END_1BYTE: /* could be SysEx end (1-byte) OR standard 1-byte System Common (Tune Request) */
            if (in_sysex && b1 == MIDI_SYSEX_END && sysex_data_len < MAX_SYSEX_DATA)
            {
                sysex_data[sysex_data_len++] = b1;
                finalize_sysex();
            }
            else if (b1 == MIDI_TUNE_REQUEST)
            {
                // handle_tune_request();
                PRINT("Handle tune request\r\n");
            }
            break;

        case MIDI_CIN_SYSEX_END_2BYTE: /* SysEx end (2-byte) or empty SysEx */
            if (b1 == MIDI_SYSEX_START && !in_sysex)
            {
                // Rare: entire 2-byte SysEx (F0 F7) — Empty SysEx
                // process_sysex(...);
                PRINT("Empty SysEx\r\n");
                sysex_data_len = 0;
                in_sysex = 0;
            }
            else if (in_sysex && b2 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 1))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                finalize_sysex();
            }
            break;

        case MIDI_CIN_SYSEX_END_3BYTE:
            if (in_sysex && b3 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
                finalize_sysex();
            }
            break;

        default:
            /* others ignored */
            break;
    }
}


/* never returns */
void midi_run(void)
{
    uint8_t midi_pkt[4];
    uint8_t previous_kb_result[GAME_ROWS] = {0};
    uint8_t current_kb_result[GAME_ROWS] = {0};

    memset(midi_pkt, 0, 4);

    board_clear_leds();
    board_show_leds();

    /* wait until the button that started this mode is released, so it is not sent to the host */
    do
    {
        board_read_buttons(current_kb_result);
    } while (memcmp(previous_kb_result, current_kb_result, GAME_ROWS) != 0);

    while (1)
    {
        /* take a local copy of the current button state */
        board_read_buttons(current_kb_result);

        if (USB_available())
        {
            if (USB_read(midi_pkt, 4) == 4)
            {
                handle_midi(midi_pkt[0] & MIDI_CIN_MASK, midi_pkt[1], midi_pkt[2], midi_pkt[3]);
            }
        }

        if (memcmp(previous_kb_result, current_kb_result, GAME_ROWS) != 0)
        {
            for (int c = 0; c < GAME_COLS; c++)
            {
                for (int r = 0; r < GAME_ROWS; r++)
                {
                    uint8_t current_button_state = (current_kb_result[r] & GAME_BUTTON(c)) & 0xff;
                    uint8_t previous_button_state = (previous_kb_result[r] & GAME_BUTTON(c)) & 0xff;

                    if (current_button_state != previous_button_state)
                    {
                        USBSendControlChange(MIDI_CHANNEL, ((r << 4) & 0xF0) | c, current_button_state ? MIDI_MAX : 0);
                    }
                }
            }
            /* update the previous button state */
            memcpy(previous_kb_result, current_kb_result, GAME_ROWS);
        }

        if (flag_update_leds)
        {
            flag_update_leds = 0;
            /* set the current led state to the leds */
            board_show_leds();
        }
    }
}
