#ifndef __GAMES_H
#define __GAMES_H

#include <stdint.h>

#include "board.h"

/* Games are the modes that can be started from the startup menu, see main.c.
 * Coordinates follow the USB-MIDI protocol: row 0 is the top row, col 0 the left column.
 */
#define GAME_ROWS     BOARD_ROWS
#define GAME_COLS     BOARD_COLS
#define GAME_FRAME_MS (20) /* duration of one game frame */
#define GAME_TEXT_ROWS (5)  /* height of the scrolling text */
#define GAME_TEXT_TOP  (2)  /* the text is always in rows 2 .. 6 */

/* bit in a button matrix row byte for the given column */
#define GAME_BUTTON(col) BOARD_BUTTON(col)

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} game_color_t;

/* players of the two player games */
#define GAME_PLAYER_NONE (0)
#define GAME_PLAYER_RED  (1) /* player 1 */
#define GAME_PLAYER_BLUE (2) /* player 2 */
#define GAME_PLAYER_DRAW (3) /* the result of a game without a winner */

typedef enum
{
    GAME_DIFFICULTY_EASY = 0,
    GAME_DIFFICULTY_NORMAL,
    GAME_DIFFICULTY_HARD,
    GAME_DIFFICULTY_COUNT
} game_difficulty_t;

extern const game_color_t game_player_colors[2]; /* indexed by player - 1 */
extern const game_color_t game_white;

/* shared game helpers, implemented in games.c */
void game_frame(void);
uint32_t game_millis(void);
void game_input_reset(void);
uint8_t game_poll_press(uint8_t *row, uint8_t *col);
void game_seed_random(uint32_t seed);
uint32_t game_random(void);
void game_set_led(uint8_t row, uint8_t col, game_color_t color);
void game_render_text(const char *text, uint32_t elapsed_ms, game_color_t color);

/* shared screens of the two player games, implemented in games.c */
uint8_t game_other_player(uint8_t player);
uint8_t game_select_players(void);
game_difficulty_t game_select_difficulty(void);
void game_show_result(uint8_t winner);
game_color_t game_scale_color(game_color_t color, uint8_t percent);
void game_render_rope(uint8_t edge, uint8_t percent);

/* the games, they return to the game menu when the game is over */
void connect_four_run(void);
void button_masher_run(void);
void tug_of_war_run(void);
void rainbow_run(void);
void life_run(void);
void tetris_run(void);
void midi_run(void); /* never returns */

#endif /* __GAMES_H */
