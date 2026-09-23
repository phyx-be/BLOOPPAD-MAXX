#include <string.h> /* memset() */

#include "debug.h"
#include "games.h"

#define TEXT_GLYPH_ROWS   (7)
#define TEXT_GLYPH_WIDTH  (5)
#define TEXT_CHAR_WIDTH   (TEXT_GLYPH_WIDTH + 1) /* glyph plus one column of spacing */
#define TEXT_SCROLL_SPEED (8)                    /* columns per second */

typedef struct
{
    char c;
    uint8_t rows[TEXT_GLYPH_ROWS]; /* 5 bits per row, MSB (bit 4) is the leftmost column */
} glyph_t;

/* 5x7 font, the same as the BloopPad Maxx playground */
static const glyph_t font[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
};

#define ROPE_BORDER_PERCENT (80) /* brightness of the columns next to the rope */
#define ROPE_FIELD_PERCENT  (20) /* brightness of the other columns */
#define RESULT_LOCK_MS      (900) /* presses on the result screen are ignored this long, so a last press does not restart */
#define PLAYERS_RED_COL     (0)             /* button to pick the number of players, player 1 color */
#define PLAYERS_BLUE_COL    (GAME_COLS - 1) /* button to pick the number of players, player 2 color */

const game_color_t game_player_colors[2] = {
    {.r = 255, .g = 0, .b = 0}, /* red */
    {.r = 0, .g = 0, .b = 255}, /* blue */
};
const game_color_t game_white = {.r = 255, .g = 255, .b = 255};

static const game_color_t difficulty_colors[GAME_DIFFICULTY_COUNT] = {
    {.r = 0, .g = 255, .b = 0},   /* easy */
    {.r = 255, .g = 183, .b = 0}, /* normal */
    {.r = 255, .g = 0, .b = 0},   /* hard */
};

static uint32_t millis = 0;
static uint32_t random_state = 0x13371337;
static uint8_t previous_buttons[GAME_ROWS];
static uint8_t pending_presses[GAME_ROWS];

/* show the current led state and wait for the next frame */
void game_frame(void)
{
    board_show_leds();
    Delay_Ms(GAME_FRAME_MS);
    millis += GAME_FRAME_MS;
}

/* approximate time since the game started, advanced by game_frame() */
uint32_t game_millis(void)
{
    return millis;
}

/* forget all presses so far; buttons that are held now only count after a release */
void game_input_reset(void)
{
    board_read_buttons(previous_buttons);
    memset(pending_presses, 0, GAME_ROWS);
}

/* get the next newly pressed button, returns 0 if there is none */
uint8_t game_poll_press(uint8_t *row, uint8_t *col)
{
    uint8_t buttons[GAME_ROWS];

    board_read_buttons(buttons);
    for (int r = 0; r < GAME_ROWS; r++)
    {
        pending_presses[r] |= buttons[r] & ~previous_buttons[r];
        previous_buttons[r] = buttons[r];
    }

    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            if (pending_presses[r] & GAME_BUTTON(c))
            {
                pending_presses[r] &= ~GAME_BUTTON(c);
                *row = r;
                *col = c;
                return 1;
            }
        }
    }
    return 0;
}

void game_seed_random(uint32_t seed)
{
    random_state ^= seed;
    if (random_state == 0)
    {
        random_state = 0x13371337; /* xorshift must not be seeded with 0 */
    }
}

/* xorshift32 pseudo random number */
uint32_t game_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

void game_set_led(uint8_t row, uint8_t col, game_color_t color)
{
    board_set_led(row, col, color.r, color.g, color.b);
}

static const glyph_t *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(font) / sizeof(font[0]); i++)
    {
        if (font[i].c == c)
        {
            return &font[i];
        }
    }
    return find_glyph('?');
}

/* draw a text scrolling from right to left in rows top_row .. top_row + 6, it starts at the left edge and loops */
void game_render_text(const char *text, uint32_t elapsed_ms, game_color_t color, uint8_t top_row)
{
    uint32_t text_width = strlen(text) * TEXT_CHAR_WIDTH;
    uint32_t offset = ((elapsed_ms * TEXT_SCROLL_SPEED) / 1000 + GAME_COLS) % (text_width + GAME_COLS);

    board_clear_leds();
    for (int x = 0; x < GAME_COLS; x++)
    {
        int32_t sx = x + (int32_t)offset - GAME_COLS;
        if (sx < 0 || sx >= (int32_t)text_width || (sx % TEXT_CHAR_WIDTH) >= TEXT_GLYPH_WIDTH)
        {
            continue;
        }

        const glyph_t *glyph = find_glyph(text[sx / TEXT_CHAR_WIDTH]);
        for (int y = 0; y < TEXT_GLYPH_ROWS && (top_row + y) < GAME_ROWS; y++)
        {
            if (glyph->rows[y] & (1 << (TEXT_GLYPH_WIDTH - 1 - (sx % TEXT_CHAR_WIDTH))))
            {
                game_set_led(top_row + y, x, color);
            }
        }
    }
}

uint8_t game_other_player(uint8_t player)
{
    return player == GAME_PLAYER_RED ? GAME_PLAYER_BLUE : GAME_PLAYER_RED;
}

/* let the players pick one player (press the red or the blue button on the first row)
 * or two players (press both simultaneously); decided when all buttons are released
 * returns the color of the single player, or GAME_PLAYER_NONE for two players
 */
uint8_t game_select_players(void)
{
    const uint8_t mask = GAME_BUTTON(PLAYERS_RED_COL) | GAME_BUTTON(PLAYERS_BLUE_COL);
    uint8_t buttons[GAME_ROWS];
    uint8_t pressed_together = 0;
    uint8_t pressed = 0;

    /* the game menu button may still be held, and it can be one of the player buttons */
    do
    {
        board_clear_leds();
        game_frame();
        board_read_buttons(buttons);
    } while (buttons[0] & mask);

    while (1)
    {
        board_clear_leds();
        game_set_led(0, PLAYERS_RED_COL, game_player_colors[GAME_PLAYER_RED - 1]);
        game_set_led(0, PLAYERS_BLUE_COL, game_player_colors[GAME_PLAYER_BLUE - 1]);
        game_frame();

        board_read_buttons(buttons);
        uint8_t held = buttons[0] & mask;

        if (held)
        {
            pressed |= held;
            if (held == mask)
            {
                pressed_together = 1;
            }
        }
        else if (pressed)
        {
            if (pressed_together)
            {
                return GAME_PLAYER_NONE;
            }
            /* one button was pressed and released before the other one (if any) was pressed */
            return (pressed & GAME_BUTTON(PLAYERS_RED_COL)) ? GAME_PLAYER_RED : GAME_PLAYER_BLUE;
        }
    }
}

/* let the player pick the difficulty of the computer on the first row */
game_difficulty_t game_select_difficulty(void)
{
    uint8_t row;
    uint8_t col;

    game_input_reset();
    while (1)
    {
        board_clear_leds();
        for (int i = 0; i < GAME_DIFFICULTY_COUNT; i++)
        {
            game_set_led(0, i, difficulty_colors[i]);
        }
        game_frame();

        if (game_poll_press(&row, &col) && row == 0 && col < GAME_DIFFICULTY_COUNT)
        {
            /* the moment of the press is the best source of randomness we have */
            game_seed_random(game_millis() * 2654435761u);
            return (game_difficulty_t)col;
        }
    }
}

/* scroll the winner (or "DRAW") until a button is pressed */
void game_show_result(uint8_t winner)
{
    uint8_t row;
    uint8_t col;
    uint32_t start = game_millis();
    const char *text = winner == GAME_PLAYER_RED ? "RED WINS" : winner == GAME_PLAYER_BLUE ? "BLUE WINS" : "DRAW";
    game_color_t color = winner == GAME_PLAYER_DRAW ? game_white : game_player_colors[winner - 1];

    while (1)
    {
        game_render_text(text, game_millis() - start, color, 0);
        game_frame();

        if (game_millis() - start < RESULT_LOCK_MS)
        {
            game_input_reset();
        }
        else if (game_poll_press(&row, &col))
        {
            return;
        }
    }
}

game_color_t game_scale_color(game_color_t color, uint8_t percent)
{
    game_color_t result = {
        .r = (color.r * percent) / 100,
        .g = (color.g * percent) / 100,
        .b = (color.b * percent) / 100,
    };
    return result;
}

/* draw the two halves of the duel games: red left of edge, blue from edge on;
 * the columns next to the rope are brighter, percent dims everything
 */
void game_render_rope(uint8_t edge, uint8_t percent)
{
    for (int col = 0; col < GAME_COLS; col++)
    {
        uint8_t side = col < edge ? GAME_PLAYER_RED : GAME_PLAYER_BLUE;
        uint8_t border = (col == edge - 1) || (col == edge);
        uint8_t scale = ((border ? ROPE_BORDER_PERCENT : ROPE_FIELD_PERCENT) * percent) / 100;
        game_color_t color = game_scale_color(game_player_colors[side - 1], scale);

        for (int row = 0; row < GAME_ROWS; row++)
        {
            game_set_led(row, col, color);
        }
    }
}
