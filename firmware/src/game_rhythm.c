/* Rhythm Tap: lights race across the rows towards the green target column on
 * the left. Press the target button of that row the moment the light arrives:
 * green = perfect, yellow = good, red = it got away.
 */
#include <stdio.h> /* snprintf() */

#include "debug.h"
#include "games.h"

#define RT_SUB         (64)  /* sub-position units per cell */
#define RT_TARGET_COL  (0)
#define RT_NOTES       (30)  /* notes per round */

/* light speed in sub-units per frame (50 fps) and the spawn interval */
static const int16_t rt_speed[GAME_DIFFICULTY_COUNT] = {4, 6, 8};   /* 3.1 / 4.7 / 6.3 cells per second */
static const uint16_t rt_spawn_ms[GAME_DIFFICULTY_COUNT] = {900, 700, 520};
static const uint16_t rt_flash_ms = (200);

static const game_color_t rt_target_color = {.r = 0, .g = 90, .b = 0};    /* the target column */
static const game_color_t rt_note_color = {.r = 255, .g = 255, .b = 255}; /* the racing light */
static const game_color_t rt_perfect_color = {.r = 0, .g = 255, .b = 0};
static const game_color_t rt_good_color = {.r = 255, .g = 255, .b = 0};
static const game_color_t rt_miss_color = {.r = 255, .g = 0, .b = 0};

#define RT_PERFECT_DIST ((RT_SUB * 3) / 5) /* within 0.6 cells of the target */
#define RT_GOOD_DIST    (RT_SUB + RT_SUB / 2) /* within 1.5 cells of the target */

typedef struct
{
    uint8_t active;
    uint8_t row;
    int32_t pos; /* position in sub-units, 7.0 at the right edge, 0 at the target */
} rt_note_t;

/* a feedback flash on a target button */
typedef struct
{
    uint32_t until;
    game_color_t color;
} rt_flash_t;

void rhythm_run(void)
{
    game_difficulty_t difficulty;
    rt_note_t notes[GAME_ROWS] = {0}; /* one note per row is plenty at these speeds */
    rt_flash_t flashes[GAME_ROWS] = {0};
    uint16_t score = 0;
    uint8_t spawned = 0;
    uint32_t next_spawn;
    uint32_t start;
    uint32_t elapsed;
    uint8_t row;
    uint8_t col;
    char result[24];

    difficulty = game_select_difficulty();

    game_input_reset();
    start = game_millis();
    next_spawn = 0; /* the first note appears immediately */

    while (1)
    {
        elapsed = game_millis() - start;

        /* spawn the next note in a free row */
        if (spawned < RT_NOTES && elapsed >= next_spawn)
        {
            for (int tries = 0; tries < GAME_ROWS; tries++)
            {
                uint8_t r = game_random() % GAME_ROWS;
                if (!notes[r].active)
                {
                    notes[r].active = 1;
                    notes[r].row = r;
                    notes[r].pos = (GAME_COLS - 1) * RT_SUB;
                    spawned++;
                    break;
                }
            }
            next_spawn = elapsed + rt_spawn_ms[difficulty];
        }

        /* move the lights */
        for (int r = 0; r < GAME_ROWS; r++)
        {
            if (notes[r].active)
            {
                notes[r].pos -= rt_speed[difficulty];
                if (notes[r].pos < -RT_SUB)
                {
                    /* it got away */
                    notes[r].active = 0;
                    flashes[r].until = elapsed + rt_flash_ms;
                    flashes[r].color = rt_miss_color;
                }
            }
        }

        /* judge the presses on the target column */
        while (game_poll_press(&row, &col))
        {
            if (col != RT_TARGET_COL || !notes[row].active)
            {
                continue;
            }

            int32_t dist = notes[row].pos < 0 ? -notes[row].pos : notes[row].pos;
            if (dist <= RT_PERFECT_DIST)
            {
                score += 100;
                flashes[row].color = rt_perfect_color;
            }
            else if (dist <= RT_GOOD_DIST)
            {
                score += 50;
                flashes[row].color = rt_good_color;
            }
            else
            {
                continue; /* way too early, the note keeps coming */
            }

            notes[row].active = 0;
            flashes[row].until = elapsed + rt_flash_ms;
        }

        /* draw */
        board_clear_leds();
        for (int r = 0; r < GAME_ROWS; r++)
        {
            if (notes[r].active && notes[r].pos >= 0 && notes[r].pos <= (GAME_COLS - 1) * RT_SUB)
            {
                game_set_led(r, notes[r].pos / RT_SUB, rt_note_color);
            }
        }
        for (int r = 0; r < GAME_ROWS; r++)
        {
            if (elapsed < flashes[r].until)
            {
                game_set_led(r, RT_TARGET_COL, flashes[r].color);
            }
            else
            {
                game_set_led(r, RT_TARGET_COL, rt_target_color);
            }
        }
        game_frame();

        /* done when every note was spawned and handled */
        if (spawned >= RT_NOTES)
        {
            uint8_t any_active = 0;
            for (int r = 0; r < GAME_ROWS; r++)
            {
                any_active |= notes[r].active;
            }
            if (!any_active)
            {
                break;
            }
        }
    }

    snprintf(result, sizeof(result), "SCORE %u", (unsigned)score);
    game_show_text(result, rt_perfect_color);
}
