/* Minesweeper: press to reveal a cell, press and hold to plant a flag.
 * The first reveal is always safe. Clear every cell without a mine to win.
 */
#include <string.h> /* memcpy() */

#include "debug.h"
#include "games.h"

#define MS_HOLD_FLAG_MS (400)  /* hold this long to flag a cell */
#define MS_BLINK_MS     (300)  /* blink period of a flag */
#define MS_END_SHOW_MS  (2000) /* how long the final board is shown */

/* number of mines per difficulty */
static const uint8_t ms_mines[GAME_DIFFICULTY_COUNT] = {8, 12, 16};

/* the classic minesweeper number colors, 0 is the revealed empty cell */
static const game_color_t ms_number_colors[9] = {
    {.r = 0, .g = 0, .b = 0},       /* 0: revealed, off */
    {.r = 30, .g = 60, .b = 255},   /* 1: blue */
    {.r = 0, .g = 255, .b = 0},     /* 2: green */
    {.r = 255, .g = 0, .b = 0},     /* 3: red */
    {.r = 200, .g = 0, .b = 255},   /* 4: magenta */
    {.r = 255, .g = 120, .b = 0},   /* 5: orange */
    {.r = 0, .g = 255, .b = 255},   /* 6: cyan */
    {.r = 255, .g = 255, .b = 255}, /* 7: white */
    {.r = 120, .g = 120, .b = 120}, /* 8: grey */
};

static const game_color_t ms_hidden_color = {.r = 25, .g = 25, .b = 25};   /* not revealed yet */
static const game_color_t ms_flag_color = {.r = 255, .g = 255, .b = 0};    /* a planted flag */
static const game_color_t ms_mine_color = {.r = 255, .g = 0, .b = 0};      /* a mine */

typedef struct
{
    uint8_t mines[GAME_ROWS][GAME_COLS];    /* 1 if this cell has a mine */
    uint8_t numbers[GAME_ROWS][GAME_COLS];  /* adjacent mine count */
    uint8_t revealed[GAME_ROWS][GAME_COLS]; /* 1 if revealed */
    uint8_t flags[GAME_ROWS][GAME_COLS];    /* 1 if flagged */
    uint8_t mines_left;
    uint8_t revealed_count;
} ms_state_t;

/* place the mines everywhere except the given cell, then compute the numbers */
static void ms_place_mines(ms_state_t *state, uint8_t safe_row, uint8_t safe_col)
{
    memset(state->mines, 0, sizeof(state->mines));

    while (state->mines_left > 0)
    {
        uint8_t r = game_random() % GAME_ROWS;
        uint8_t c = game_random() % GAME_COLS;

        if (state->mines[r][c] || (r == safe_row && c == safe_col))
        {
            continue;
        }
        state->mines[r][c] = 1;
        state->mines_left--;
    }

    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            uint8_t n = 0;
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    int rr = r + dr;
                    int cc = c + dc;
                    if (rr >= 0 && rr < GAME_ROWS && cc >= 0 && cc < GAME_COLS && state->mines[rr][cc])
                    {
                        n++;
                    }
                }
            }
            state->numbers[r][c] = n;
        }
    }
}

/* reveal a cell, flood fill into the neighbours of empty cells */
static void ms_reveal(ms_state_t *state, uint8_t row, uint8_t col)
{
    uint8_t stack[GAME_ROWS * GAME_COLS];
    int top = 0;

    if (state->revealed[row][col] || state->flags[row][col])
    {
        return;
    }

    stack[top++] = (row << 4) | col;
    while (top > 0)
    {
        uint8_t cell = stack[--top];
        uint8_t r = cell >> 4;
        uint8_t c = cell & 0x0f;

        if (r >= GAME_ROWS || c >= GAME_COLS || state->revealed[r][c] || state->flags[r][c] || state->mines[r][c])
        {
            continue;
        }

        state->revealed[r][c] = 1;
        state->revealed_count++;

        if (state->numbers[r][c] == 0)
        {
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    int rr = r + dr;
                    int cc = c + dc;
                    if (rr >= 0 && rr < GAME_ROWS && cc >= 0 && cc < GAME_COLS && !state->revealed[rr][cc])
                    {
                        stack[top++] = (rr << 4) | cc;
                    }
                }
            }
        }
    }
}

static void ms_render(const ms_state_t *state, uint32_t now_ms)
{
    board_clear_leds();
    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            if (state->flags[r][c] && !state->revealed[r][c])
            {
                /* flags blink so they stand out from the hidden cells */
                if ((now_ms % MS_BLINK_MS) < (MS_BLINK_MS / 2))
                {
                    game_set_led(r, c, ms_flag_color);
                }
            }
            else if (state->revealed[r][c])
            {
                game_set_led(r, c, ms_number_colors[state->numbers[r][c]]);
            }
            else
            {
                game_set_led(r, c, ms_hidden_color);
            }
        }
    }
}

/* show every mine, flash them, then scroll the result text */
static void ms_explode(const ms_state_t *state)
{
    for (int f = 0; f < 6; f++)
    {
        board_clear_leds();
        if ((f % 2) == 0)
        {
            for (int r = 0; r < GAME_ROWS; r++)
            {
                for (int c = 0; c < GAME_COLS; c++)
                {
                    if (state->mines[r][c])
                    {
                        game_set_led(r, c, ms_mine_color);
                    }
                }
            }
        }
        game_frame();
    }
    game_show_text("BOOM", ms_mine_color);
}

/* blink the whole board green, then scroll the result text */
static void ms_cleared(const ms_state_t *state)
{
    const game_color_t green = {.r = 0, .g = 255, .b = 0};

    for (int f = 0; f < 6; f++)
    {
        board_clear_leds();
        if ((f % 2) == 0)
        {
            for (int r = 0; r < GAME_ROWS; r++)
            {
                for (int c = 0; c < GAME_COLS; c++)
                {
                    if (!state->mines[r][c])
                    {
                        game_set_led(r, c, green);
                    }
                }
            }
        }
        game_frame();
    }
    game_show_text("CLEARED", green);
}

void minesweeper_run(void)
{
    static ms_state_t state; /* static: too large for the stack */
    game_difficulty_t difficulty;
    uint8_t prev[GAME_ROWS] = {0};
    uint8_t buttons[GAME_ROWS];
    uint8_t tracking = 0; /* a press is being tracked for press-or-hold */
    uint8_t track_row = 0;
    uint8_t track_col = 0;
    uint8_t track_handled = 0; /* the hold already triggered a flag */
    uint32_t press_start = 0;
    uint8_t first_reveal = 1;
    uint8_t total_safe;

    difficulty = game_select_difficulty();
    memset(&state, 0, sizeof(state));
    state.mines_left = ms_mines[difficulty];
    total_safe = GAME_ROWS * GAME_COLS - state.mines_left;

    while (1)
    {
        uint32_t now = game_millis();

        board_read_buttons(buttons);

        /* start tracking the first newly pressed button */
        if (!tracking)
        {
            for (int r = 0; r < GAME_ROWS && !tracking; r++)
            {
                for (int c = 0; c < GAME_COLS; c++)
                {
                    if ((buttons[r] & GAME_BUTTON(c)) && !(prev[r] & GAME_BUTTON(c)))
                    {
                        tracking = 1;
                        track_row = r;
                        track_col = c;
                        track_handled = 0;
                        press_start = now;
                        break;
                    }
                }
            }
        }

        if (tracking)
        {
            uint8_t held = buttons[track_row] & GAME_BUTTON(track_col);

            /* holding a hidden cell plants a flag */
            if (held && !track_handled && !state.revealed[track_row][track_col] && (now - press_start) >= MS_HOLD_FLAG_MS)
            {
                if (!first_reveal) /* flagging is pointless before the first reveal */
                {
                    state.flags[track_row][track_col] ^= 1;
                }
                track_handled = 1;
            }

            /* on release, an unhandled press reveals the cell */
            if (!held)
            {
                if (!track_handled && !state.flags[track_row][track_col])
                {
                    if (first_reveal)
                    {
                        /* the first reveal is always safe */
                        ms_place_mines(&state, track_row, track_col);
                        first_reveal = 0;
                    }

                    if (state.mines[track_row][track_col])
                    {
                        ms_explode(&state);
                        memcpy(prev, buttons, GAME_ROWS);
                        return;
                    }

                    ms_reveal(&state, track_row, track_col);
                }
                tracking = 0;
            }
        }

        memcpy(prev, buttons, GAME_ROWS);

        if (!first_reveal && state.revealed_count == total_safe)
        {
            ms_cleared(&state);
            return;
        }

        ms_render(&state, now);
        game_frame();
    }
}
