/* Game of life, ported from the BloopPad Maxx playground.
 *
 * Every button is a cell. Draw a starting pattern, then let it evolve: a cell with 3 living
 * neighbors is born, a living cell with 2 or 3 living neighbors survives, every other cell dies.
 * As in the playground the board does not wrap around, and a new generation follows every 0.3 s.
 *
 * Controls:
 *   tap a button:                  toggle that cell (also while running)
 *   hold one button for 1 s:       start or pause; starting an empty board seeds a random one
 *   hold two buttons for 1 s:      return to the game menu
 * While paused the living cells slowly pulse. Held buttons light up white and get brighter
 * until the hold counts.
 */
#include <string.h> /* memcpy(), memset() */

#include "games.h"

#define LIFE_STEP_MS        (300) /* time between generations, as in the playground */
#define LIFE_HOLD_MS        (1000) /* how long buttons must be held to start/pause or quit */
#define LIFE_RANDOM_DENSITY (30)   /* percentage of living cells in a random board, as in the playground */
#define LIFE_PULSE_MS       (2000) /* period of the pulsing cells while paused */
#define LIFE_PULSE_MIN      (30)   /* lowest brightness (percent) of the pulsing cells */
#define LIFE_HELD_MIN       (15)   /* brightness (percent) of a held button when the hold starts */

static const game_color_t life_color = {.r = 77, .g = 221, .b = 207}; /* the playground's teal */

static uint8_t cells[GAME_ROWS]; /* a set bit (see GAME_BUTTON) is a living cell */
static uint8_t running;
static uint32_t last_step_ms;

static uint8_t life_alive(const uint8_t *board, int row, int col)
{
    if (row < 0 || row >= GAME_ROWS || col < 0 || col >= GAME_COLS)
    {
        return 0;
    }
    return (board[row] & GAME_BUTTON(col)) != 0;
}

static uint8_t life_empty(void)
{
    for (int row = 0; row < GAME_ROWS; row++)
    {
        if (cells[row])
        {
            return 0;
        }
    }
    return 1;
}

static void life_seed_random(void)
{
    for (int row = 0; row < GAME_ROWS; row++)
    {
        cells[row] = 0;
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (game_random() % 100 < LIFE_RANDOM_DENSITY)
            {
                cells[row] |= GAME_BUTTON(col);
            }
        }
    }
}

static void life_step(void)
{
    uint8_t next[GAME_ROWS];

    for (int row = 0; row < GAME_ROWS; row++)
    {
        next[row] = 0;
        for (int col = 0; col < GAME_COLS; col++)
        {
            uint8_t neighbors = 0;
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    if (dr || dc)
                    {
                        neighbors += life_alive(cells, row + dr, col + dc);
                    }
                }
            }

            if (neighbors == 3 || (neighbors == 2 && life_alive(cells, row, col)))
            {
                next[row] |= GAME_BUTTON(col);
            }
        }
    }
    memcpy(cells, next, GAME_ROWS);
}

static void life_toggle_run(void)
{
    running = !running;
    if (running)
    {
        if (life_empty())
        {
            /* the moment of the hold is the best source of randomness we have */
            game_seed_random(game_millis() * 2654435761u);
            life_seed_random();
        }
        last_step_ms = game_millis();
    }
}

static uint8_t count_held(const uint8_t *buttons)
{
    uint8_t count = 0;

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (uint8_t bits = buttons[row]; bits; bits &= bits - 1)
        {
            count++;
        }
    }
    return count;
}

/* the living cells (pulsing while paused), with the held buttons on top of them */
static void life_render(const uint8_t *held, uint32_t held_ms)
{
    uint8_t percent = 100;
    uint8_t held_percent = LIFE_HELD_MIN + ((100 - LIFE_HELD_MIN) * (held_ms < LIFE_HOLD_MS ? held_ms : LIFE_HOLD_MS)) / LIFE_HOLD_MS;

    if (!running)
    {
        /* triangle wave between LIFE_PULSE_MIN and 100% */
        uint32_t phase = game_millis() % LIFE_PULSE_MS;
        uint32_t level = phase < LIFE_PULSE_MS / 2 ? phase : LIFE_PULSE_MS - phase;
        percent = LIFE_PULSE_MIN + ((100 - LIFE_PULSE_MIN) * level) / (LIFE_PULSE_MS / 2);
    }

    board_clear_leds();
    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (held[row] & GAME_BUTTON(col))
            {
                game_set_led(row, col, game_scale_color(game_white, held_percent));
            }
            else if (cells[row] & GAME_BUTTON(col))
            {
                game_set_led(row, col, game_scale_color(life_color, percent));
            }
        }
    }
}

void life_run(void)
{
    uint8_t buttons[GAME_ROWS];
    uint8_t previous[GAME_ROWS];
    uint8_t held_changed = 0;
    uint8_t consumed; /* the buttons that are held now were used for a hold, their release does not toggle */
    uint32_t hold_start_ms = game_millis();

    memset(cells, 0, sizeof(cells));
    running = 0;

    /* the game menu button may still be held, it only counts after a release */
    board_read_buttons(previous);
    consumed = count_held(previous) > 0;

    while (1)
    {
        board_read_buttons(buttons);

        for (int row = 0; row < GAME_ROWS; row++)
        {
            uint8_t released = previous[row] & ~buttons[row];
            if (buttons[row] != previous[row])
            {
                held_changed = 1;
            }
            if (!consumed)
            {
                /* a tap toggles the cell when it is released, so a hold does not change it */
                cells[row] ^= released;
            }
        }
        memcpy(previous, buttons, GAME_ROWS);

        uint8_t held_count = count_held(buttons);
        if (held_count == 0)
        {
            consumed = 0;
        }
        if (held_changed)
        {
            /* the hold starts over whenever a button is pressed or released */
            held_changed = 0;
            hold_start_ms = game_millis();
        }

        uint32_t held_ms = game_millis() - hold_start_ms;
        if (held_count && !consumed && held_ms >= LIFE_HOLD_MS)
        {
            if (held_count >= 2)
            {
                return;
            }
            consumed = 1;
            life_toggle_run();
        }

        if (running && game_millis() - last_step_ms >= LIFE_STEP_MS)
        {
            last_step_ms += LIFE_STEP_MS;
            life_step();
        }

        life_render(buttons, consumed ? 0 : held_ms);
        game_frame();
    }
}
