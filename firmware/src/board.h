#ifndef __BOARD_H
#define __BOARD_H

#include <stdint.h>

/* BloopPad Maxx hardware: an 8x8 button matrix with a WS2812 LED under every button.
 * Coordinates follow the USB-MIDI protocol: row 0 is the top row, col 0 the left column.
 */
#define BOARD_ROWS (8)
#define BOARD_COLS (8)

/* bit in a button matrix row byte for the given column */
#define BOARD_BUTTON(col) (1 << (BOARD_COLS - 1 - (col)))

/* initialize the LEDs and start the button matrix scan */
void board_init(void);

/* set the color of one LED, it is shown by board_show_leds() */
void board_set_led(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b);
void board_clear_leds(void);
void board_show_leds(void);

/* copy the debounced button state, one byte per row, a set bit (see BOARD_BUTTON) is a pressed button */
void board_read_buttons(uint8_t rows[BOARD_ROWS]);

#endif /* __BOARD_H */
