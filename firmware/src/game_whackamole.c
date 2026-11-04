/* Whack-a-Mole: moles pop up in your color, smack them before they hide
 * again. Hitting an empty button in your half costs a point. Most points
 * after 30 seconds wins.
 */
#include <stdio.h> /* snprintf() */

#include "debug.h"
#include "games.h"

#define WM_GAME_MS   (30000) /* duration of a round */
#define WM_BLINK_MS  (150)   /* flash of a whacked mole */

/* mole lifetime and spawn interval at the start and at the end of the round */
static const uint16_t wm_lifetime_start[GAME_DIFFICULTY_COUNT] = {1300, 1100, 900};
static const uint16_t wm_lifetime_end[GAME_DIFFICULTY_COUNT] = {700, 550, 450};
static const uint16_t wm_spawn_start[GAME_DIFFICULTY_COUNT] = {1000, 850, 700};
static const uint16_t wm_spawn_end[GAME_DIFFICULTY_COUNT] = {450, 380, 300};

static const game_color_t wm_whack_color = {.r = 255, .g = 255, .b = 255};
static const game_color_t wm_solo_color = {.r = 255, .g = 60, .b = 0};

/* one mole slot per half of the board */
typedef struct
{
    uint8_t active;
    uint8_t row;
    uint8_t col;
    uint32_t deadline;
} wm_mole_t;

/* a bright flash where a mole was whacked */
typedef struct
{
    uint32_t until;
    uint8_t row;
    uint8_t col;
} wm_flash_t;

static uint16_t wm_lerp(const uint16_t *start, const uint16_t *end, game_difficulty_t d, uint32_t elapsed)
{
    /* linearly shrink from the start value to the end value over the round */
    return (uint16_t)(start[d] + ((int32_t)end[d] - (int32_t)start[d]) * (int32_t)elapsed / WM_GAME_MS);
}

/* the lowest column of a half: the red half is left, the blue half is right */
static uint8_t wm_half_of(uint8_t col)
{
    return col < GAME_COLS / 2 ? GAME_PLAYER_RED : GAME_PLAYER_BLUE;
}

void whack_a_mole_run(void)
{
    uint8_t single_player;
    game_difficulty_t difficulty;
    wm_mole_t moles[2] = {0};   /* index 0 = red half, 1 = blue half */
    uint32_t next_spawn[2] = {0};
    wm_flash_t flashes[2] = {0};
    uint16_t scores[2] = {0};   /* indexed like the moles */
    uint32_t start;
    uint32_t elapsed;
    uint8_t row;
    uint8_t col;
    char result[24];

    single_player = game_select_players();
    if (single_player != GAME_PLAYER_NONE)
    {
        difficulty = game_select_difficulty();
    }
    else
    {
        difficulty = GAME_DIFFICULTY_NORMAL; /* a duel runs at a fixed pace */
    }

    game_input_reset();
    start = game_millis();

    while (1)
    {
        elapsed = game_millis() - start;
        if (elapsed >= WM_GAME_MS)
        {
            break;
        }

        /* pop up a new mole in each half when it is time */
        for (int half = 0; half < 2; half++)
        {
            /* in a duel both halves are used, in solo mode moles pop up on the whole board */
            if (!moles[half].active && elapsed >= next_spawn[half] && (half == 0 || single_player == GAME_PLAYER_NONE))
            {
                uint8_t min_col = (single_player != GAME_PLAYER_NONE || half == 0) ? 0 : GAME_COLS / 2;
                uint8_t max_col = (single_player != GAME_PLAYER_NONE || half == 1) ? GAME_COLS - 1 : GAME_COLS / 2 - 1;

                moles[half].active = 1;
                moles[half].row = game_random() % GAME_ROWS;
                moles[half].col = min_col + (game_random() % (max_col - min_col + 1));
                moles[half].deadline = elapsed + wm_lerp(wm_lifetime_start, wm_lifetime_end, difficulty, elapsed);
            }

            if (moles[half].active && elapsed >= moles[half].deadline)
            {
                moles[half].active = 0; /* it got away */
                next_spawn[half] = elapsed + wm_lerp(wm_spawn_start, wm_spawn_end, difficulty, elapsed);
            }
        }

        /* handle the presses */
        if (game_poll_press(&row, &col))
        {
            uint8_t half = wm_half_of(col);

            if (single_player != GAME_PLAYER_NONE)
            {
                half = 0; /* solo: everything scores in one bucket */
            }

            if (moles[half].active && moles[half].row == row && moles[half].col == col)
            {
                scores[half]++;
                moles[half].active = 0;
                flashes[half].row = row;
                flashes[half].col = col;
                flashes[half].until = elapsed + WM_BLINK_MS;
                next_spawn[half] = elapsed + wm_lerp(wm_spawn_start, wm_spawn_end, difficulty, elapsed);
            }
            else
            {
                /* an empty button in this half: a miss costs a point */
                if (scores[half] > 0)
                {
                    scores[half]--;
                }
            }
        }

        /* draw */
        board_clear_leds();
        for (int half = 0; half < 2; half++)
        {
            if (moles[half].active)
            {
                game_color_t color = half == 0
                    ? (single_player != GAME_PLAYER_NONE ? wm_solo_color : game_player_colors[GAME_PLAYER_RED - 1])
                    : game_player_colors[GAME_PLAYER_BLUE - 1];
                game_set_led(moles[half].row, moles[half].col, color);
            }
            if (elapsed < flashes[half].until)
            {
                game_set_led(flashes[half].row, flashes[half].col, wm_whack_color);
            }
        }
        game_frame();
    }

    /* the result */
    if (single_player == GAME_PLAYER_NONE)
    {
        if (scores[0] > scores[1])
        {
            game_show_result(GAME_PLAYER_RED);
        }
        else if (scores[1] > scores[0])
        {
            game_show_result(GAME_PLAYER_BLUE);
        }
        else
        {
            game_show_result(GAME_PLAYER_DRAW);
        }
    }
    else
    {
        snprintf(result, sizeof(result), "SCORE %u", (unsigned)scores[0]);
        game_show_text(result, wm_solo_color);
    }
}
