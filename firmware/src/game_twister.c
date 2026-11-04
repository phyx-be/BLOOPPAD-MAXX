/* Finger Twister: buttons light up that you must hold - all of them at the
 * same time, while new ones keep appearing. A newly lit button may be grabbed
 * at leisure, but a button you were holding must not be let go. Only possible
 * thanks to the n-key rollover of the button matrix.
 */
#include <stdio.h> /* snprintf() */

#include "debug.h"
#include "games.h"

#define FT_MAX_TARGETS  (8)
#define FT_GRAB_MS      (2500) /* time to grab a newly lit button */
#define FT_GRACE_MS     (350)  /* time an established hold may be released */
#define FT_BLINK_MS     (250)  /* blink of the newest button */

/* how many buttons to hold at once and how fast new ones appear */
static const uint8_t ft_target_holds[GAME_DIFFICULTY_COUNT] = {4, 6, 8};
static const uint16_t ft_spawn_ms[GAME_DIFFICULTY_COUNT] = {2000, 1500, 1100};

static const game_color_t ft_palette[] = {
    {.r = 255, .g = 0, .b = 0},
    {.r = 0, .g = 255, .b = 0},
    {.r = 0, .g = 80, .b = 255},
    {.r = 255, .g = 255, .b = 0},
    {.r = 0, .g = 255, .b = 255},
    {.r = 255, .g = 0, .b = 255},
    {.r = 255, .g = 120, .b = 0},
    {.r = 255, .g = 255, .b = 255},
};

static const game_color_t ft_win_color = {.r = 0, .g = 255, .b = 0};
static const game_color_t ft_lose_color = {.r = 255, .g = 0, .b = 0};

typedef struct
{
    uint8_t row;
    uint8_t col;
    uint16_t unheld_ms;  /* how long it has been released (0 = held) */
    uint8_t established; /* 1 once it was held at least once */
} ft_target_t;

/* returns the number of targets that are currently held */
static uint8_t ft_held_count(const ft_target_t *targets, uint8_t count)
{
    uint8_t held = 0;

    for (int i = 0; i < count; i++)
    {
        if (targets[i].unheld_ms == 0)
        {
            held++;
        }
    }
    return held;
}

void twister_run(void)
{
    game_difficulty_t difficulty;
    ft_target_t targets[FT_MAX_TARGETS] = {0};
    uint8_t count = 0;
    uint8_t palette_idx = game_random() % (sizeof(ft_palette) / sizeof(ft_palette[0]));
    uint32_t next_spawn;
    uint32_t now;
    uint8_t buttons[GAME_ROWS];
    char result[24];
    uint8_t failed = 0;
    uint8_t won = 0;

    difficulty = game_select_difficulty();

    /* wait until the button that started this game is released */
    do
    {
        board_read_buttons(buttons);
        game_frame();
    } while (buttons[0] || buttons[1] || buttons[2] || buttons[3] || buttons[4] || buttons[5] || buttons[6] || buttons[7]);

    game_input_reset();
    next_spawn = game_millis() + 1000; /* the first target appears after a second */

    while (!failed && !won)
    {
        now = game_millis();
        board_read_buttons(buttons);

        /* check that every target is held */
        for (int i = 0; i < count; i++)
        {
            if (buttons[targets[i].row] & GAME_BUTTON(targets[i].col))
            {
                targets[i].unheld_ms = 0;
                targets[i].established = 1;
            }
            else
            {
                uint16_t limit = targets[i].established ? FT_GRACE_MS : FT_GRAB_MS;
                if (targets[i].unheld_ms < limit)
                {
                    targets[i].unheld_ms += GAME_FRAME_MS;
                }
                if (targets[i].unheld_ms >= limit)
                {
                    failed = 1;
                }
            }
        }

        /* spawn a new target when everything is held */
        if (!failed && count < ft_target_holds[difficulty] && ft_held_count(targets, count) == count && now >= next_spawn)
        {
            for (int tries = 0; tries < 32; tries++)
            {
                uint8_t r = game_random() % GAME_ROWS;
                uint8_t c = game_random() % GAME_COLS;
                uint8_t taken = 0;

                for (int i = 0; i < count; i++)
                {
                    if (targets[i].row == r && targets[i].col == c)
                    {
                        taken = 1;
                    }
                }
                if (!taken && !(buttons[r] & GAME_BUTTON(c))) /* do not spawn under a finger */
                {
                    targets[count].row = r;
                    targets[count].col = c;
                    targets[count].unheld_ms = GAME_FRAME_MS + 1; /* not held yet */
                    targets[count].established = 0;
                    count++;
                    palette_idx = (palette_idx + 1) % (sizeof(ft_palette) / sizeof(ft_palette[0]));
                    next_spawn = now + ft_spawn_ms[difficulty];
                    break;
                }
            }
        }

        /* holding everything: the game is won */
        if (count >= ft_target_holds[difficulty] && ft_held_count(targets, count) == count)
        {
            won = 1;
        }

        /* draw: the newest target blinks to draw the eye */
        board_clear_leds();
        for (int i = 0; i < count; i++)
        {
            uint8_t is_newest = (i == count - 1) && !targets[i].established;
            if (!is_newest || (now % FT_BLINK_MS) < (FT_BLINK_MS / 2))
            {
                game_set_led(targets[i].row, targets[i].col, ft_palette[(palette_idx + i) % (sizeof(ft_palette) / sizeof(ft_palette[0]))]);
            }
        }
        game_frame();
    }

    if (won)
    {
        game_show_text("YOU WIN", ft_win_color);
    }
    else
    {
        snprintf(result, sizeof(result), "SCORE %u", (unsigned)(count > 0 ? count - 1 : 0));
        game_show_text(result, ft_lose_color);
    }
}
