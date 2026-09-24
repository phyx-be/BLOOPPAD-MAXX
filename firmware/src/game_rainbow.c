/* Rainbow: not really a game, it shows LED animations.
 *
 * The buttons on the first row pick one of the 8 animations:
 *   col 0: rainbow wave (as in the BloopPad Maxx playground)
 *   col 1: plasma
 *   col 2: fire
 *   col 3: matrix rain
 *   col 4: sparkles
 *   col 5: spinning rainbow spiral
 *   col 6: ripples
 *   col 7: game of life
 * Any other button returns to the game menu.
 *
 * The chip has no floating point unit: angles are in 1/256 (or 1/65536) of a turn
 * and distances in 1/16 of a cell, with precomputed tables.
 */
#include <string.h> /* memset() */

#include "games.h"

#define RB_CELLS (GAME_ROWS * GAME_COLS)

/* (sin(2 * pi * i / 256) * 0.5 + 0.5) * 230: the playground's rainbow brightness */
static const uint8_t sine[256] = {
    115, 118, 121, 123, 126, 129, 132, 135, 137, 140, 143, 146, 148, 151, 154, 156,
    159, 162, 164, 167, 169, 172, 174, 177, 179, 181, 184, 186, 188, 190, 192, 194,
    196, 198, 200, 202, 204, 206, 207, 209, 211, 212, 214, 215, 216, 218, 219, 220,
    221, 222, 223, 224, 225, 226, 227, 227, 228, 228, 229, 229, 229, 230, 230, 230,
    230, 230, 230, 230, 229, 229, 229, 228, 228, 227, 227, 226, 225, 224, 223, 222,
    221, 220, 219, 218, 216, 215, 214, 212, 211, 209, 207, 206, 204, 202, 200, 198,
    196, 194, 192, 190, 188, 186, 184, 181, 179, 177, 174, 172, 169, 167, 164, 162,
    159, 156, 154, 151, 148, 146, 143, 140, 137, 135, 132, 129, 126, 123, 121, 118,
    115, 112, 109, 107, 104, 101, 98, 95, 93, 90, 87, 84, 82, 79, 76, 74,
    71, 68, 66, 63, 61, 58, 56, 53, 51, 49, 46, 44, 42, 40, 38, 36,
    34, 32, 30, 28, 26, 24, 23, 21, 19, 18, 16, 15, 14, 12, 11, 10,
    9, 8, 7, 6, 5, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 14, 15, 16, 18, 19, 21, 23, 24, 26, 28, 30, 32,
    34, 36, 38, 40, 42, 44, 46, 49, 51, 53, 56, 58, 61, 63, 66, 68,
    71, 74, 76, 79, 82, 84, 87, 90, 93, 95, 98, 101, 104, 107, 109, 112,
};

/* angle (1/256 turn) of every cell around the center of the board, row major */
static const uint8_t center_angle[RB_CELLS] = {
    160, 167, 176, 186, 198, 208, 217, 224,
    153, 160, 170, 184, 200, 214, 224, 231,
    144, 150, 160, 179, 205, 224, 234, 240,
    134, 136, 141, 160, 224, 243, 248, 250,
    122, 120, 115, 96, 32, 13, 8, 6,
    112, 106, 96, 77, 51, 32, 22, 16,
    103, 96, 86, 72, 56, 42, 32, 25,
    96, 89, 80, 70, 58, 48, 39, 32,
};

/* distance (1/16 cell) of every cell from the center of the board, row major */
static const uint8_t center_distance[RB_CELLS] = {
    79, 69, 61, 57, 57, 61, 69, 79,
    69, 57, 47, 41, 41, 47, 57, 69,
    61, 47, 34, 25, 25, 34, 47, 61,
    57, 41, 25, 11, 11, 25, 41, 57,
    57, 41, 25, 11, 11, 25, 41, 57,
    61, 47, 34, 25, 25, 34, 47, 61,
    69, 57, 47, 41, 41, 47, 57, 69,
    79, 69, 61, 57, 57, 61, 69, 79,
};

/* distance (1/16 cell) between two cells, indexed by the row and column difference */
static const uint8_t distance[GAME_ROWS][GAME_COLS] = {
    {0, 16, 32, 48, 64, 80, 96, 112},
    {16, 23, 36, 51, 66, 82, 97, 113},
    {32, 36, 45, 58, 72, 86, 101, 116},
    {48, 51, 58, 68, 80, 93, 107, 122},
    {64, 66, 72, 80, 91, 102, 115, 129},
    {80, 82, 86, 93, 102, 113, 125, 138},
    {96, 97, 101, 107, 115, 125, 136, 148},
    {112, 113, 116, 122, 129, 138, 148, 158},
};

/* the playground's rainbow: red, green and blue are sines that are 2 radians apart; angle in 1/65536 turn */
static game_color_t rainbow_color16(uint16_t angle)
{
    game_color_t color = {
        .r = sine[angle >> 8],
        .g = sine[(uint16_t)(angle + 20861) >> 8], /* + 2 radians */
        .b = sine[(uint16_t)(angle + 41722) >> 8], /* + 4 radians */
    };
    return color;
}

/* rainbow color, angle in 1/256 turn */
static game_color_t rainbow_color(uint8_t angle)
{
    return rainbow_color16(angle << 8);
}

/* scale a color with a level of 0 .. 255 */
static game_color_t dim(game_color_t color, uint8_t level)
{
    game_color_t result = {
        .r = (color.r * level) / 255,
        .g = (color.g * level) / 255,
        .b = (color.b * level) / 255,
    };
    return result;
}

/* angle in 1/65536 turn after turning for ms at turns_per_second (in 1/65536 turn per second), wraps around */
static uint16_t turned(uint32_t ms, uint16_t turns_per_second)
{
    return (uint16_t)((ms / 1000) * turns_per_second + ((ms % 1000) * turns_per_second) / 1000);
}

static int16_t abs16(int16_t value)
{
    return value < 0 ? -value : value;
}

/* 0: rainbow wave, as in the playground: phase = x * 0.35 + y * 0.4 - 2 * t (radians) */
static void wave_render(uint32_t ms)
{
    uint16_t t = turned(ms, 20861); /* 2 radians per second */

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            game_set_led(row, col, rainbow_color16(col * 3651 + row * 4172 - t)); /* 0.35 and 0.4 radians */
        }
    }
}

/* 1: plasma: the sum of moving sine waves picks the color */
static void plasma_render(uint32_t ms)
{
    uint8_t t = turned(ms, 8192) >> 8; /* 1/8 turn per second */

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            uint16_t value = sine[(uint8_t)(col * 32 + t)] +
                             sine[(uint8_t)(row * 24 - 2 * t)] +
                             sine[(uint8_t)((col + row) * 20 + 3 * t)] +
                             sine[(uint8_t)(center_distance[row * GAME_COLS + col] * 3 - 2 * t)];
            game_set_led(row, col, rainbow_color((uint8_t)(value / 2 + t)));
        }
    }
}

/* 2: fire: heat rises from the bottom row and cools down */
#define FIRE_STEP_MS    (50)
#define FIRE_COOLING    (80) /* maximum random heat loss per row */

static uint8_t heat[GAME_ROWS][GAME_COLS];
static uint32_t fire_last_step;

static void fire_init(void)
{
    memset(heat, 0, sizeof(heat));
    fire_last_step = 0;
}

static void fire_step(void)
{
    /* from the top down, so every row is calculated from the old rows below it */
    for (int row = 0; row < GAME_ROWS - 1; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            uint8_t below = heat[row + 1][col];
            uint8_t left = col > 0 ? heat[row + 1][col - 1] : below;
            uint8_t right = col < GAME_COLS - 1 ? heat[row + 1][col + 1] : below;
            uint8_t below2 = row < GAME_ROWS - 2 ? heat[row + 2][col] : below;
            int16_t value = (left + 2 * below + right + below2) / 5 - (int16_t)(game_random() % FIRE_COOLING);

            heat[row][col] = value < 0 ? 0 : value;
        }
    }

    /* the flickering fuel on the bottom row */
    for (int col = 0; col < GAME_COLS; col++)
    {
        heat[GAME_ROWS - 1][col] = 128 + game_random() % 128;
    }
}

static void fire_render(uint32_t ms)
{
    while (ms - fire_last_step >= FIRE_STEP_MS)
    {
        fire_last_step += FIRE_STEP_MS;
        fire_step();
    }

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            /* black - red - yellow - white */
            uint8_t h = heat[row][col];
            game_color_t color = {
                .r = h < 85 ? h * 3 : 255,
                .g = h < 85 ? 0 : h < 170 ? (h - 85) * 3 : 255,
                .b = h < 170 ? 0 : (h - 170) * 3,
            };
            game_set_led(row, col, color);
        }
    }
}

/* 3: matrix rain: green drops fall down the columns and leave a fading trail */
#define RAIN_FADE  (240) /* trail brightness kept per frame, of 256 */
#define RAIN_TRAIL (4)   /* rows below the board before a drop restarts */

static uint8_t rain[GAME_ROWS][GAME_COLS];
static int8_t drop_row[GAME_COLS];
static uint8_t drop_frames[GAME_COLS]; /* frames per row: the speed of the drop */
static uint8_t drop_count[GAME_COLS];

static void rain_new_drop(uint8_t col)
{
    drop_row[col] = -(int8_t)(game_random() % 8);
    drop_frames[col] = 2 + game_random() % 4;
    drop_count[col] = 0;
}

static void rain_init(void)
{
    memset(rain, 0, sizeof(rain));
    for (int col = 0; col < GAME_COLS; col++)
    {
        rain_new_drop(col);
    }
}

static void rain_render(uint32_t ms)
{
    (void)ms;

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            rain[row][col] = (rain[row][col] * RAIN_FADE) >> 8;
        }
    }

    for (int col = 0; col < GAME_COLS; col++)
    {
        if (++drop_count[col] >= drop_frames[col])
        {
            drop_count[col] = 0;
            drop_row[col]++;
            if (drop_row[col] >= GAME_ROWS + RAIN_TRAIL)
            {
                rain_new_drop(col);
            }
        }
        if (drop_row[col] >= 0 && drop_row[col] < GAME_ROWS)
        {
            rain[drop_row[col]][col] = 255;
        }
    }

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            uint8_t level = rain[row][col];
            game_color_t color = {.r = 0, .g = level, .b = 0};

            if (row == drop_row[col])
            {
                /* the head of the drop is almost white */
                color.r = 160;
                color.b = 160;
            }
            game_set_led(row, col, color);
        }
    }
}

/* 4: sparkles: random pixels flash up in a random color and fade out */
#define SPARKLE_FADE   (245) /* brightness kept per frame, of 256 */
#define SPARKLE_CHANCE (80)  /* percentage of frames with a new sparkle */

static uint8_t sparkle_level[RB_CELLS];
static uint8_t sparkle_hue[RB_CELLS];

static void sparkle_init(void)
{
    memset(sparkle_level, 0, sizeof(sparkle_level));
}

static void sparkle_render(uint32_t ms)
{
    (void)ms;

    for (int i = 0; i < RB_CELLS; i++)
    {
        sparkle_level[i] = (sparkle_level[i] * SPARKLE_FADE) >> 8;
    }

    if (game_random() % 100 < SPARKLE_CHANCE)
    {
        uint8_t i = game_random() % RB_CELLS;
        sparkle_level[i] = 255;
        sparkle_hue[i] = game_random();
    }

    for (int i = 0; i < RB_CELLS; i++)
    {
        /* squared: bright flashes with a soft tail */
        uint8_t level = (sparkle_level[i] * sparkle_level[i]) >> 8;
        game_set_led(i / GAME_COLS, i % GAME_COLS, dim(rainbow_color(sparkle_hue[i]), level));
    }
}

/* 5: spinning rainbow spiral: the color follows the angle around the center, twisted by the distance */
static void spiral_render(uint32_t ms)
{
    uint8_t t = turned(ms, 16384) >> 8; /* 1/4 turn per second */

    for (int i = 0; i < RB_CELLS; i++)
    {
        game_set_led(i / GAME_COLS, i % GAME_COLS, rainbow_color(center_angle[i] + center_distance[i] - t));
    }
}

/* 6: ripples: colored rings spread out from random spots, like drops in a pond */
#define RIPPLES          (3)
#define RIPPLE_SPEED     (96)  /* 1/16 cells per second: 6 cells per second */
#define RIPPLE_WIDTH     (32)  /* 1/16 cells: the ring fades out up to 2 cells from its radius */
#define RIPPLE_CORE      (12)  /* 1/16 cells: full brightness this close to the radius */
#define RIPPLE_MAX       (160) /* 1/16 cells: the ripple is gone at 10 cells */
#define RIPPLE_EVERY_MS  (500) /* a new ripple every 0.5 .. 1.2 s */
#define RIPPLE_RANDOM_MS (700)

typedef struct
{
    uint8_t row;
    uint8_t col;
    uint8_t hue;
    uint8_t active;
    uint32_t start;
} ripple_t;

static ripple_t ripples[RIPPLES];
static uint8_t ripple_next;
static uint32_t ripple_next_ms;

static void ripple_init(void)
{
    memset(ripples, 0, sizeof(ripples));
    ripple_next = 0;
    ripple_next_ms = 0;
}

static void ripple_render(uint32_t ms)
{
    static uint16_t sum[RB_CELLS][3];

    if (ms >= ripple_next_ms)
    {
        ripple_t *ripple = &ripples[ripple_next];
        ripple->row = game_random() % GAME_ROWS;
        ripple->col = game_random() % GAME_COLS;
        ripple->hue = game_random();
        ripple->active = 1;
        ripple->start = ms;
        ripple_next = (ripple_next + 1) % RIPPLES;
        ripple_next_ms = ms + RIPPLE_EVERY_MS + game_random() % RIPPLE_RANDOM_MS;
    }

    /* add up the rings, they may overlap */
    memset(sum, 0, sizeof(sum));
    for (int r = 0; r < RIPPLES; r++)
    {
        ripple_t *ripple = &ripples[r];
        uint32_t radius = ((ms - ripple->start) * RIPPLE_SPEED) / 1000;

        if (!ripple->active)
        {
            continue;
        }
        if (radius >= RIPPLE_MAX)
        {
            ripple->active = 0;
            continue;
        }

        game_color_t color = rainbow_color(ripple->hue);
        uint8_t fade = 255 - (radius * 255) / RIPPLE_MAX;

        for (int i = 0; i < RB_CELLS; i++)
        {
            uint8_t d = distance[abs16(i / GAME_COLS - ripple->row)][abs16(i % GAME_COLS - ripple->col)];
            int16_t off = abs16((int16_t)d - (int16_t)radius);

            if (off < RIPPLE_WIDTH)
            {
                uint8_t level = off <= RIPPLE_CORE ? fade : ((RIPPLE_WIDTH - off) * fade) / (RIPPLE_WIDTH - RIPPLE_CORE);
                game_color_t c = dim(color, level);
                sum[i][0] += c.r;
                sum[i][1] += c.g;
                sum[i][2] += c.b;
            }
        }
    }

    for (int i = 0; i < RB_CELLS; i++)
    {
        board_set_led(i / GAME_COLS, i % GAME_COLS,
                      sum[i][0] > 255 ? 255 : sum[i][0],
                      sum[i][1] > 255 ? 255 : sum[i][1],
                      sum[i][2] > 255 ? 255 : sum[i][2]);
    }
}

/* 7: game of life on a board that wraps around; the color shows the age of a cell.
 * A new random board is seeded when it dies out, stops changing or after a while.
 */
#define LIFE_STEP_MS         (250)
#define LIFE_MAX_GENERATIONS (150)
#define LIFE_DENSITY         (35) /* percentage of living cells in a new board */
#define LIFE_AGE_HUE         (12) /* hue change per generation of age */

static uint8_t life[GAME_ROWS]; /* bit col is set for a living cell */
static uint8_t life_previous[GAME_ROWS];
static uint8_t life_age[RB_CELLS];
static uint8_t life_hue;
static uint16_t life_generation;
static uint32_t life_last_step;

static uint8_t life_alive(const uint8_t *board, int row, int col)
{
    row = (row + GAME_ROWS) % GAME_ROWS;
    col = (col + GAME_COLS) % GAME_COLS;
    return (board[row] >> col) & 1;
}

static void life_seed(void)
{
    for (int row = 0; row < GAME_ROWS; row++)
    {
        life[row] = 0;
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (game_random() % 100 < LIFE_DENSITY)
            {
                life[row] |= 1 << col;
            }
        }
    }
    memset(life_previous, 0, sizeof(life_previous));
    memset(life_age, 0, sizeof(life_age));
    life_hue = game_random();
    life_generation = 0;
}

static void life_init(void)
{
    life_seed();
    life_last_step = 0;
}

static void life_step(void)
{
    uint8_t next[GAME_ROWS];
    uint8_t any_alive = 0;

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
                        neighbors += life_alive(life, row + dr, col + dc);
                    }
                }
            }

            uint8_t alive = life_alive(life, row, col);
            uint8_t *age = &life_age[row * GAME_COLS + col];
            if (neighbors == 3 || (alive && neighbors == 2))
            {
                next[row] |= 1 << col;
                any_alive = 1;
                if (!alive)
                {
                    *age = 0; /* born */
                }
                else if (*age < 255)
                {
                    (*age)++; /* survived */
                }
            }
            else
            {
                *age = 0;
            }
        }
    }

    /* dead, still, blinking between two states, or running too long: start over */
    if (!any_alive || memcmp(next, life, GAME_ROWS) == 0 || memcmp(next, life_previous, GAME_ROWS) == 0 ||
        ++life_generation >= LIFE_MAX_GENERATIONS)
    {
        life_seed();
        return;
    }

    memcpy(life_previous, life, GAME_ROWS);
    memcpy(life, next, GAME_ROWS);
}

static void life_render(uint32_t ms)
{
    while (ms - life_last_step >= LIFE_STEP_MS)
    {
        life_last_step += LIFE_STEP_MS;
        life_step();
    }

    for (int row = 0; row < GAME_ROWS; row++)
    {
        for (int col = 0; col < GAME_COLS; col++)
        {
            if (life_alive(life, row, col))
            {
                uint8_t age = life_age[row * GAME_COLS + col];
                game_set_led(row, col, rainbow_color(life_hue + age * LIFE_AGE_HUE));
            }
        }
    }
}

typedef struct
{
    void (*init)(void);           /* NULL if there is nothing to reset */
    void (*render)(uint32_t ms); /* draw the frame at ms since the start of the animation */
} animation_t;

/* the animations, indexed by the column of their button on the first row */
static const animation_t animations[GAME_COLS] = {
    {.init = NULL, .render = wave_render},
    {.init = NULL, .render = plasma_render},
    {.init = fire_init, .render = fire_render},
    {.init = rain_init, .render = rain_render},
    {.init = sparkle_init, .render = sparkle_render},
    {.init = NULL, .render = spiral_render},
    {.init = ripple_init, .render = ripple_render},
    {.init = life_init, .render = life_render},
};

static const animation_t *rb_start(uint8_t index, uint32_t *start)
{
    if (animations[index].init)
    {
        animations[index].init();
    }
    *start = game_millis();
    return &animations[index];
}

void rainbow_run(void)
{
    uint8_t row;
    uint8_t col;
    uint32_t start;
    const animation_t *animation = rb_start(0, &start);

    game_input_reset();
    while (1)
    {
        while (game_poll_press(&row, &col))
        {
            if (row != 0)
            {
                return;
            }
            animation = rb_start(col, &start);
        }

        board_clear_leds();
        animation->render(game_millis() - start);
        game_frame();
    }
}
