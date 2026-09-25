/* Tetris, ported from the BloopPad Maxx playground.
 *
 * The whole board is the playing field; the buttons of the bottom row are the controls:
 *   col 0:       move left
 *   col 1:       turn counterclockwise
 *   col 2 .. 5:  move down
 *   col 6:       turn clockwise
 *   col 7:       move right
 * Holding a move button repeats it. A full row is cleared; the blocks fall faster as more
 * rows are cleared.
 * When a new block does not fit, the game is over: the score scrolls by, press any button
 * to return to the game menu.
 */
#include <string.h> /* memcpy(), memset() */

#include "games.h"

#define TT_ROWS           GAME_ROWS
#define TT_CONTROL_ROW    (GAME_ROWS - 1) /* the buttons of this row are the controls */
#define TT_SHAPE_COUNT    (7)
#define TT_SHAPE_SIZE     (4)
#define TT_REPEAT_DELAY   (250) /* a held move button repeats after this many ms */
#define TT_REPEAT_MS      (80)  /* and then every this many ms */
#define TT_FLASH_MS       (900) /* the game over flash, as in the playground */

typedef enum
{
    TT_NONE = 0,
    TT_LEFT,
    TT_RIGHT,
    TT_TURN_LEFT,
    TT_TURN_RIGHT,
    TT_DOWN,
} tt_action_t;

/* the action of each button on the control row */
static const tt_action_t controls[GAME_COLS] = {
    TT_LEFT, TT_TURN_LEFT, TT_DOWN, TT_DOWN, TT_DOWN, TT_DOWN, TT_TURN_RIGHT, TT_RIGHT,
};

typedef struct
{
    uint8_t width;
    uint8_t height;
    uint8_t cells[TT_SHAPE_SIZE][TT_SHAPE_SIZE]; /* [row][col] */
} tt_piece_t;

/* the shapes of the playground: I, O, T, L, J, S, Z */
static const tt_piece_t shapes[TT_SHAPE_COUNT] = {
    {4, 1, {{1, 1, 1, 1}}},
    {2, 2, {{1, 1}, {1, 1}}},
    {3, 2, {{0, 1, 0}, {1, 1, 1}}},
    {3, 2, {{1, 0, 0}, {1, 1, 1}}},
    {3, 2, {{0, 0, 1}, {1, 1, 1}}},
    {3, 2, {{0, 1, 1}, {1, 1, 0}}},
    {3, 2, {{1, 1, 0}, {0, 1, 1}}},
};

/* saturated colors, indexed by shape: the classic Tetris colors */
static const game_color_t palette[TT_SHAPE_COUNT] = {
    {.r = 0, .g = 255, .b = 255},   /* I: cyan */
    {.r = 255, .g = 255, .b = 0},   /* O: yellow */
    {.r = 160, .g = 0, .b = 255},   /* T: purple */
    {.r = 255, .g = 100, .b = 0},   /* L: orange */
    {.r = 0, .g = 0, .b = 255},     /* J: blue */
    {.r = 0, .g = 255, .b = 0},     /* S: green */
    {.r = 255, .g = 0, .b = 0},     /* Z: red */
};

static const game_color_t flash_color = {.r = 255, .g = 90, .b = 155};

/* the playground's score for clearing 0 .. 4 rows at once */
static const uint16_t line_scores[5] = {0, 100, 300, 500, 800};

static uint8_t field[TT_ROWS][GAME_COLS]; /* 0 is empty, else the palette index + 1 */
static tt_piece_t piece;
static uint8_t piece_color;
static int8_t piece_row;
static int8_t piece_col;
static uint8_t next_shape;
static uint16_t lines;
static uint32_t score;
static uint8_t over;
static uint32_t last_drop_ms;

static uint8_t tt_fits(const tt_piece_t *p, int row, int col)
{
    for (int r = 0; r < p->height; r++)
    {
        for (int c = 0; c < p->width; c++)
        {
            if (!p->cells[r][c])
            {
                continue;
            }
            int fr = row + r;
            int fc = col + c;
            if (fr < 0 || fr >= TT_ROWS || fc < 0 || fc >= GAME_COLS || field[fr][fc])
            {
                return 0;
            }
        }
    }
    return 1;
}

static void tt_spawn(void)
{
    piece = shapes[next_shape];
    piece_color = next_shape;
    next_shape = game_random() % TT_SHAPE_COUNT;
    piece_row = 0;
    piece_col = (GAME_COLS - piece.width) / 2;
    if (!tt_fits(&piece, piece_row, piece_col))
    {
        over = 1;
    }
}

/* the time between two automatic drops: max(0.12, 0.7 - lines * 0.025) s, as in the playground */
static uint32_t tt_drop_ms(void)
{
    uint32_t faster = lines * 25;
    return faster >= 700 - 120 ? 120 : 700 - faster;
}

static void tt_clear_lines(void)
{
    uint8_t cleared = 0;

    for (int row = TT_ROWS - 1; row >= 0; row--)
    {
        uint8_t full = 1;
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (!field[row][col])
            {
                full = 0;
                break;
            }
        }
        if (!full)
        {
            continue;
        }

        /* move everything above one row down, and check this row again */
        memmove(field[1], field[0], row * GAME_COLS);
        memset(field[0], 0, GAME_COLS);
        cleared++;
        row++;
    }

    lines += cleared;
    score += line_scores[cleared];
}

/* move the piece one row down, or lock it when it can not; returns 1 if it moved */
static uint8_t tt_drop(void)
{
    last_drop_ms = game_millis();
    if (tt_fits(&piece, piece_row + 1, piece_col))
    {
        piece_row++;
        return 1;
    }

    for (int r = 0; r < piece.height; r++)
    {
        for (int c = 0; c < piece.width; c++)
        {
            if (piece.cells[r][c])
            {
                field[piece_row + r][piece_col + c] = piece_color + 1;
            }
        }
    }
    tt_clear_lines();
    tt_spawn();
    return 0;
}

static void tt_turn(uint8_t clockwise)
{
    tt_piece_t turned = {.width = piece.height, .height = piece.width};
    static const int8_t kicks[] = {0, -1, 1, -2, 2}; /* as in the playground */

    for (int r = 0; r < turned.height; r++)
    {
        for (int c = 0; c < turned.width; c++)
        {
            turned.cells[r][c] = clockwise ? piece.cells[piece.height - 1 - c][r] : piece.cells[c][piece.width - 1 - r];
        }
    }

    for (unsigned i = 0; i < sizeof(kicks); i++)
    {
        if (tt_fits(&turned, piece_row, piece_col + kicks[i]))
        {
            piece = turned;
            piece_col += kicks[i];
            return;
        }
    }
}

static void tt_action(tt_action_t action)
{
    switch (action)
    {
        case TT_LEFT:
        case TT_RIGHT:
        {
            int8_t col = piece_col + (action == TT_LEFT ? -1 : 1);
            if (tt_fits(&piece, piece_row, col))
            {
                piece_col = col;
            }
            break;
        }
        case TT_TURN_LEFT:
        case TT_TURN_RIGHT:
            tt_turn(action == TT_TURN_RIGHT);
            break;
        case TT_DOWN:
            tt_drop();
            break;
        case TT_NONE:
            break;
    }
}

static void tt_render(void)
{
    board_clear_leds();

    for (int row = 0; row < TT_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (field[row][col])
            {
                game_set_led(row, col, palette[field[row][col] - 1]);
            }
        }
    }

    for (int r = 0; r < piece.height; r++)
    {
        for (int c = 0; c < piece.width; c++)
        {
            if (piece.cells[r][c])
            {
                game_set_led(piece_row + r, piece_col + c, palette[piece_color]);
            }
        }
    }
}

/* the playground's game over: one soft pulse with an expanding diamond */
static void tt_render_flash(uint32_t elapsed_ms)
{
    /* t in 1/256 of the flash */
    int32_t t = (elapsed_ms * 256) / TT_FLASH_MS;
    /* the diamond's radius in 1/2 cell: t * (width + height) / 2 */
    int32_t radius = (t * (GAME_COLS + GAME_ROWS)) / 256;

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            /* distance from the center, in 1/2 cell */
            int32_t dr = 2 * row - (GAME_ROWS - 1);
            int32_t dc = 2 * col - (GAME_COLS - 1);
            int32_t distance = (dr < 0 ? -dr : dr) + (dc < 0 ? -dc : dc);
            int32_t off = distance - radius;
            off = off < 0 ? -off : off;
            /* ring = max(0, 1 - |distance - radius| / 2), in 1/256 */
            int32_t ring = off >= 4 ? 0 : 256 - off * 64;
            /* glow = (0.15 + 0.65 * ring) * (1 - t) + 0.2 * sin(pi * t), the sine as a parabola */
            int32_t glow = ((38 + (166 * ring) / 256) * (256 - t)) / 256 + (51 * 4 * t * (256 - t)) / (256 * 256);
            if (glow < 0)
            {
                glow = 0;
            }
            game_set_led(row, col, game_scale_color(flash_color, (glow * 100) / 256));
        }
    }
}

static void tt_score_text(char *text)
{
    char digits[10];
    int count = 0;
    uint32_t value = score;

    do
    {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);

    strcpy(text, "SCORE ");
    text += strlen(text);
    while (count)
    {
        *text++ = digits[--count];
    }
    *text = '\0';
}

/* the flash, then the score scrolls by until a button is pressed */
static void tt_game_over(void)
{
    char text[20];
    uint8_t row;
    uint8_t col;
    uint32_t start = game_millis();

    tt_score_text(text);
    while (1)
    {
        uint32_t elapsed = game_millis() - start;
        if (elapsed < TT_FLASH_MS)
        {
            tt_render_flash(elapsed);
            /* presses during the flash are ignored, so a last press does not return right away */
            game_input_reset();
        }
        else
        {
            game_render_text(text, elapsed - TT_FLASH_MS, game_white, 0);
            if (game_poll_press(&row, &col))
            {
                return;
            }
        }
        game_frame();
    }
}

void tetris_run(void)
{
    uint8_t buttons[GAME_ROWS];
    uint8_t previous = 0;
    uint32_t repeat_ms[GAME_COLS];

    memset(field, 0, sizeof(field));
    lines = 0;
    score = 0;
    over = 0;
    next_shape = game_random() % TT_SHAPE_COUNT;
    tt_spawn();
    last_drop_ms = game_millis();

    while (!over)
    {
        board_read_buttons(buttons);
        uint8_t held = buttons[TT_CONTROL_ROW];
        uint8_t pressed = held & ~previous;
        previous = held;

        for (int col = 0; col < GAME_COLS && !over; col++)
        {
            tt_action_t action = controls[col];
            if (pressed & GAME_BUTTON(col))
            {
                tt_action(action);
                repeat_ms[col] = game_millis() + TT_REPEAT_DELAY;
            }
            else if ((held & GAME_BUTTON(col)) && action != TT_TURN_LEFT && action != TT_TURN_RIGHT &&
                     (int32_t)(game_millis() - repeat_ms[col]) >= 0)
            {
                tt_action(action);
                repeat_ms[col] += TT_REPEAT_MS;
            }
        }

        if (!over && game_millis() - last_drop_ms >= tt_drop_ms())
        {
            tt_drop();
        }

        if (!over)
        {
            tt_render();
            game_frame();
        }
    }

    tt_game_over();
}
