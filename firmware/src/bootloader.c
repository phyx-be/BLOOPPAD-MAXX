/* Software entry into WCH's ISP bootloader - see bootloader.h for the message
 * and why it looks the way it does.
 */
#include <ch32x035.h>

#include <wch_usbmidi_internal.h>

#include "board.h"
#include "bootloader.h"
#include "debug.h"

/* USB-MIDI Code Index Numbers that carry SysEx bytes, USB MIDI spec table 4-1.
 * Kept local rather than shared with game_midi.c's copy, so that this file does
 * not depend on a mode that may not be running. */
#define CIN_MASK            (0x0F)
#define CIN_SYSEX_START     (0x04) /* SysEx starts or continues: three bytes */
#define CIN_SYSEX_END_1BYTE (0x05) /* one byte, or a 1-byte System Common */
#define CIN_SYSEX_END_2BYTE (0x06) /* two bytes */
#define CIN_SYSEX_END_3BYTE (0x07) /* three bytes */

/* The one message this file reacts to. */
static const uint8_t bootloader_request[] = {0xF0, 0x13, 0x37, 0x00, 0x42, 0x4F, 0x4F, 0xF7};
#define BOOTLOADER_REQUEST_LEN (sizeof(bootloader_request) / sizeof(bootloader_request[0]))

/* How far into bootloader_request[] the received stream currently matches. */
static uint8_t matched = 0;

/* LED colour held through the ISP session: dim purple, which no mode uses. */
#define BOOT_LED_R (0x10)
#define BOOT_LED_G (0x00)
#define BOOT_LED_B (0x18)

#define BOOT_SETTLE_MS (50) /* let the host finish the transfer before the reset */
#define BOOT_FLASH_MS  (10) /* let the boot-mode flash write settle */

/* One byte of the SysEx stream. Advances the match, or restarts it.
 *
 * Deliberately a plain prefix match rather than a parser: an LED frame shares the
 * first three bytes and then differs (its address byte cannot be 0x00), so it
 * resets the match at index 3 and nothing else in the protocol gets close. */
static void feed_byte(uint8_t b)
{
    if (b == bootloader_request[matched])
    {
        matched++;
        if (matched == BOOTLOADER_REQUEST_LEN)
        {
            matched = 0;
            PRINT("SysEx bootloader request, rebooting into ISP\r\n");
            bootloader_enter();
        }
        return;
    }

    /* No match. A fresh F0 starts a new attempt - anything else cannot. */
    matched = (b == bootloader_request[0]) ? 1 : 0;
}

void bootloader_feed_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    switch (cin & CIN_MASK)
    {
        case CIN_SYSEX_START:
        case CIN_SYSEX_END_3BYTE:
            feed_byte(b1);
            feed_byte(b2);
            feed_byte(b3);
            break;

        case CIN_SYSEX_END_2BYTE:
            feed_byte(b1);
            feed_byte(b2);
            break;

        case CIN_SYSEX_END_1BYTE:
            feed_byte(b1);
            break;

        default:
            /* Not SysEx. Abandon any partial match: a note or a control change in
             * the middle of our message means it was not our message. */
            matched = 0;
            break;
    }
}

void bootloader_poll_usb(void)
{
    uint8_t pkt[4];

    while (USB_available() >= 4)
    {
        if (USB_read(pkt, 4) != 4)
        {
            break;
        }
        bootloader_feed_midi(pkt[0], pkt[1], pkt[2], pkt[3]);
    }
}

void bootloader_enter(void)
{
    /* Nothing refreshes a WS2812 once the application stops, so whatever is
     * written here stays lit for the whole ISP session. Without it the pad looks
     * like it is still running the game it was in. */
    for (uint8_t row = 0; row < BOARD_ROWS; row++)
    {
        for (uint8_t col = 0; col < BOARD_COLS; col++)
        {
            board_set_led(row, col, BOOT_LED_R, BOOT_LED_G, BOOT_LED_B);
        }
    }
    board_show_leds();

    Delay_Ms(BOOT_SETTLE_MS);

    SystemReset_StartMode(Start_Mode_BOOT);
    Delay_Ms(BOOT_FLASH_MS);
    NVIC_SystemReset();
}
