/* Lights Out: pressing a button toggles it and its four neighbours.
 * Turn all the LEDs off to win. The board is scrambled from the solved state,
 * so every puzzle is guaranteed to be solvable.
 */
#include <stdio.h> /* snprintf() */

#include "debug.h"
#include "games.h"

/* how many scramble presses per difficulty */
static const uint8_t lo_scrambles[GAME_DIFFICULTY_COUNT] = {3, 6, 10};

static const game_color_t lo_lit_color = {.r = 255, .g = 200, .b = 0}; /* warm amber */
static const game_color_t lo_win_color = {.r = 0, .g = 255, .b = 0};

static void lo_toggle(uint8_t lights[GAME_ROWS][GAME_COLS], uint8_t row, uint8_t col)
{
    static const int8_t plus[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    for (int i = 0; i < 5; i++)
    {
        int r = row + plus[i][0];
        int c = col + plus[i][1];
        if (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS)
        {
            lights[r][c] ^= 1;
        }
    }
}

static uint8_t lo_count_lights(const uint8_t lights[GAME_ROWS][GAME_COLS])
{
    uint8_t count = 0;

    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            count += lights[r][c];
        }
    }
    return count;
}

void lights_out_run(void)
{
    uint8_t lights[GAME_ROWS][GAME_COLS] = {0};
    game_difficulty_t difficulty;
    uint16_t moves = 0;
    uint8_t row;
    uint8_t col;
    char result[24];

    difficulty = game_select_difficulty();

    /* scramble from the solved state by playing random presses */
    for (int i = 0; i < lo_scrambles[difficulty]; i++)
    {
        lo_toggle(lights, game_random() % GAME_ROWS, game_random() % GAME_COLS);
    }

    game_input_reset();
    while (1)
    {
        board_clear_leds();
        for (int r = 0; r < GAME_ROWS; r++)
        {
            for (int c = 0; c < GAME_COLS; c++)
            {
                if (lights[r][c])
                {
                    game_set_led(r, c, lo_lit_color);
                }
            }
        }
        game_frame();

        if (!game_poll_press(&row, &col))
        {
            continue;
        }

        lo_toggle(lights, row, col);
        moves++;

        if (lo_count_lights(lights) == 0)
        {
            snprintf(result, sizeof(result), "SOLVED IN %u MOVES", (unsigned)moves);
            game_show_text(result, lo_win_color);
            return;
        }
    }
}
