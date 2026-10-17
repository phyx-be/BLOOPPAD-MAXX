/* Paddle Duel: the ball flies across the board between the two edge columns.
 * Each player holds the button on their edge where the ball arrives - holding
 * more adjacent edge buttons makes a bigger paddle (n-key rollover). First
 * player to 5 points wins.
 */
#include "debug.h"
#include "games.h"

#define PD_WIN_SCORE      (5)
#define PD_SUB            (64)                       /* sub-position units per cell */
#define PD_BALL_MAX       (PD_SUB * (GAME_COLS - 1)) /* position of the right column */
#define PD_ROW_MAX        (PD_SUB * (GAME_ROWS - 1))
#define PD_HIT_DIST       (PD_SUB / 2 + PD_SUB / 8)  /* how close the paddle must be */
#define PD_SPEED_PCT_MAX  (260)                      /* rally speed cap in percent */
#define PD_SCORE_SHOW_MS  (1200)                     /* pause between points */

static const game_color_t pd_ball_color = {.r = 255, .g = 255, .b = 255};
static const game_color_t pd_trail_color = {.r = 90, .g = 90, .b = 90};

typedef struct
{
    int32_t row;      /* ball position in sub-units (0 = row 0) */
    int32_t col;      /* ball position in sub-units (0 = col 0) */
    int32_t vrow;     /* row velocity in sub-units per frame */
    int32_t vcol;     /* column velocity in sub-units per frame, sign is the direction */
    int32_t base;     /* the base |vcol| before the rally speedup */
    int32_t speed_pct; /* rally speedup in percent */
    int32_t prev_row;
    int32_t prev_col;
} pd_ball_t;

typedef struct
{
    uint8_t score;
} pd_player_t;

/* the row of the held paddle button closest to the ball, -1 if none is held */
static int8_t pd_paddle_row(const uint8_t buttons[GAME_ROWS], uint8_t col, int32_t ball_row)
{
    int8_t best = -1;
    int32_t best_dist = 0x7fff;

    for (int r = 0; r < GAME_ROWS; r++)
    {
        if (buttons[r] & GAME_BUTTON(col))
        {
            int32_t dist = r * PD_SUB - ball_row;
            if (dist < 0)
            {
                dist = -dist;
            }
            if (dist < best_dist)
            {
                best_dist = dist;
                best = r;
            }
        }
    }
    return best;
}

static void pd_render(const pd_ball_t *ball, const uint8_t buttons[GAME_ROWS])
{
    board_clear_leds();

    /* both paddles glow in their color so the players see where they are */
    for (int r = 0; r < GAME_ROWS; r++)
    {
        if (buttons[r] & GAME_BUTTON(0))
        {
            game_set_led(r, 0, game_player_colors[GAME_PLAYER_RED - 1]);
        }
        if (buttons[r] & GAME_BUTTON(GAME_COLS - 1))
        {
            game_set_led(r, GAME_COLS - 1, game_player_colors[GAME_PLAYER_BLUE - 1]);
        }
    }

    /* the trail and the ball, skipped while they are off the board between
     * a miss and the next serve
     */
    if (ball->prev_col >= 0 && ball->prev_col <= PD_BALL_MAX)
    {
        game_set_led(ball->prev_row / PD_SUB, ball->prev_col / PD_SUB, pd_trail_color);
    }
    if (ball->col >= 0 && ball->col <= PD_BALL_MAX)
    {
        game_set_led(ball->row / PD_SUB, ball->col / PD_SUB, pd_ball_color);
    }
}

/* show both scores as bars on the middle row, then serve at the loser */
static void pd_show_score(const pd_player_t *red, const pd_player_t *blue, uint8_t serve_to)
{
    uint32_t start = game_millis();

    while (game_millis() - start < PD_SCORE_SHOW_MS)
    {
        board_clear_leds();
        for (int i = 0; i < red->score; i++)
        {
            game_set_led(GAME_ROWS / 2, i, game_player_colors[GAME_PLAYER_RED - 1]);
        }
        for (int i = 0; i < blue->score; i++)
        {
            game_set_led(GAME_ROWS / 2, GAME_COLS - 1 - i, game_player_colors[GAME_PLAYER_BLUE - 1]);
        }
        /* the blinking side is the one that serves */
        if ((game_millis() % 500) < 250)
        {
            game_set_led(GAME_ROWS / 2, serve_to == GAME_PLAYER_RED ? 0 : GAME_COLS - 1, game_white);
        }
        game_frame();
    }
}

static void pd_serve(pd_ball_t *ball, uint8_t serve_to)
{
    ball->row = (GAME_ROWS / 2) * PD_SUB;
    ball->col = (GAME_COLS / 2) * PD_SUB;
    ball->prev_row = ball->row;
    ball->prev_col = ball->col;
    ball->vrow = (((int32_t)game_random() % 3) - 1) * PD_SUB / 2; /* mostly straight, sometimes angled */
    if (ball->vrow == 0 && (game_random() & 1))
    {
        ball->vrow = PD_SUB / 2;
    }
    ball->speed_pct = 100;
    ball->vcol = serve_to == GAME_PLAYER_RED ? -ball->base : ball->base;
}

void paddle_run(void)
{
    pd_ball_t ball_data;
    pd_ball_t *ball = &ball_data;
    pd_player_t red = {.score = 0};
    pd_player_t blue = {.score = 0};
    uint8_t buttons[GAME_ROWS] = {0};
    uint8_t winner = GAME_PLAYER_DRAW;
    uint8_t any_row;

    ball->base = PD_SUB * 7 / 10; /* ~0.7 cells per frame crossing the board */

    /* wait until the button that started this game is released */
    do
    {
        board_read_buttons(buttons);
        game_frame();
        any_row = 0;
        for (int r = 0; r < GAME_ROWS; r++)
        {
            any_row |= buttons[r];
        }
    } while (any_row);

    pd_serve(ball, GAME_PLAYER_RED);
    pd_show_score(&red, &blue, GAME_PLAYER_RED);

    while (1)
    {
        board_read_buttons(buttons);

        ball->prev_row = ball->row;
        ball->prev_col = ball->col;
        ball->row += ball->vrow;
        ball->col += ball->vcol;

        /* bounce off the top and bottom */
        if (ball->row < 0)
        {
            ball->row = -ball->row;
            ball->vrow = -ball->vrow;
        }
        else if (ball->row > PD_ROW_MAX)
        {
            ball->row = 2 * PD_ROW_MAX - ball->row;
            ball->vrow = -ball->vrow;
        }

        /* the ball reaches the red side */
        if (ball->col <= 0 && ball->vcol < 0)
        {
            int8_t paddle = pd_paddle_row(buttons, 0, ball->row);
            if (paddle >= 0 && (paddle * PD_SUB - ball->row) <= PD_HIT_DIST && (ball->row - paddle * PD_SUB) <= PD_HIT_DIST)
            {
                /* bounce: the angle depends on where the paddle caught the ball */
                ball->vcol = -ball->vcol;
                ball->vrow += (ball->row - paddle * PD_SUB) / 16;
                if (ball->vrow > PD_SUB)
                {
                    ball->vrow = PD_SUB;
                }
                if (ball->vrow < -PD_SUB)
                {
                    ball->vrow = -PD_SUB;
                }
                ball->col = 0;

                /* every hit speeds the rally up */
                ball->speed_pct = ball->speed_pct * 106 / 100;
                if (ball->speed_pct > PD_SPEED_PCT_MAX)
                {
                    ball->speed_pct = PD_SPEED_PCT_MAX;
                }
                ball->vcol = ball->vcol * ball->speed_pct / 100;
            }
            else if (ball->col < -PD_SUB)
            {
                /* red missed: a point for blue */
                blue.score++;
                if (blue.score >= PD_WIN_SCORE)
                {
                    winner = GAME_PLAYER_BLUE;
                    break;
                }
                pd_serve(ball, GAME_PLAYER_RED); /* serve at the player who missed */
                pd_show_score(&red, &blue, GAME_PLAYER_RED);
            }
        }

        /* the ball reaches the blue side */
        if (ball->col >= PD_BALL_MAX && ball->vcol > 0)
        {
            int8_t paddle = pd_paddle_row(buttons, GAME_COLS - 1, ball->row);
            if (paddle >= 0 && (paddle * PD_SUB - ball->row) <= PD_HIT_DIST && (ball->row - paddle * PD_SUB) <= PD_HIT_DIST)
            {
                ball->vcol = -ball->vcol;
                ball->vrow += (ball->row - paddle * PD_SUB) / 16;
                if (ball->vrow > PD_SUB)
                {
                    ball->vrow = PD_SUB;
                }
                if (ball->vrow < -PD_SUB)
                {
                    ball->vrow = -PD_SUB;
                }
                ball->col = PD_BALL_MAX;

                ball->speed_pct = ball->speed_pct * 106 / 100;
                if (ball->speed_pct > PD_SPEED_PCT_MAX)
                {
                    ball->speed_pct = PD_SPEED_PCT_MAX;
                }
                ball->vcol = ball->vcol * ball->speed_pct / 100;
            }
            else if (ball->col > PD_BALL_MAX + PD_SUB)
            {
                /* blue missed: a point for red */
                red.score++;
                if (red.score >= PD_WIN_SCORE)
                {
                    winner = GAME_PLAYER_RED;
                    break;
                }
                pd_serve(ball, GAME_PLAYER_BLUE);
                pd_show_score(&red, &blue, GAME_PLAYER_BLUE);
            }
        }

        pd_render(ball, buttons);
        game_frame();
    }

    game_show_result(winner);
}
