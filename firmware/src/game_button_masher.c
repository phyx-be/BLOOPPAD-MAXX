/* Button masher, ported from the BloopPad Maxx playground.
 *
 * Two halves, one rope: red owns the columns left of the rope, blue the columns right of it.
 * Every press on a button in your own half pulls the rope a bit towards the other side,
 * so your half grows. Idle hands let the rope slide back to the middle.
 * Pull the rope all the way to the other edge to win.
 *
 * At the start, pick the number of players with the red and blue buttons on the first row:
 *   press one of them:         one player with that color against the computer with the other color
 *   press both simultaneously: two players on one BloopPad Maxx
 * With one player, then pick the difficulty of the computer on the first row:
 *   col 0 (green):  easy
 *   col 1 (yellow): normal
 *   col 2 (red):    hard
 * After a match, the result scrolls by; press any button to return to the game menu.
 */
#include "games.h"

/* the rope position is kept in 1/1000 of a column: 0 is the left edge, BM_ROPE_MAX the right edge */
#define BM_ROPE_SCALE        (1000)
#define BM_ROPE_MAX          (GAME_COLS * BM_ROPE_SCALE)
#define BM_ROPE_CENTER       (BM_ROPE_MAX / 2)
#define BM_PULL              (340) /* rope movement per press */
#define BM_DRIFT_PER_S       (600) /* the rope slides back to the center at this speed */
#define BM_COUNTDOWN_MS      (1200)
#define BM_COUNTDOWN_PERCENT (50) /* the halves are dimmed while waiting for the start */

/* average time between two presses of the computer, per difficulty, as in the playground */
static const uint16_t mash_interval_ms[GAME_DIFFICULTY_COUNT] = {280, 170, 100};

static game_difficulty_t difficulty;
static uint8_t two_players;
static uint8_t ai_side;
static int32_t rope;
static uint8_t winner;

static uint32_t bm_mash_interval(void)
{
    /* 75% .. 125% of the average interval */
    return (mash_interval_ms[difficulty] * (750 + game_random() % 500)) / 1000;
}

/* the column where blue's half starts */
static uint8_t bm_edge(void)
{
    return (rope + BM_ROPE_SCALE / 2) / BM_ROPE_SCALE;
}

static uint8_t bm_side_of(uint8_t col)
{
    return col < bm_edge() ? GAME_PLAYER_RED : GAME_PLAYER_BLUE;
}

static void bm_check_rope(void)
{
    if (rope >= BM_ROPE_MAX)
    {
        rope = BM_ROPE_MAX;
        winner = GAME_PLAYER_RED;
    }
    else if (rope <= 0)
    {
        rope = 0;
        winner = GAME_PLAYER_BLUE;
    }
}

static void bm_pull(uint8_t side)
{
    rope += side == GAME_PLAYER_RED ? BM_PULL : -BM_PULL;
    bm_check_rope();
}

static void bm_drift(void)
{
    int32_t drift = (BM_DRIFT_PER_S * GAME_FRAME_MS) / 1000;
    int32_t distance = BM_ROPE_CENTER - rope;

    if (distance > drift)
    {
        rope += drift;
    }
    else if (distance < -drift)
    {
        rope -= drift;
    }
    else
    {
        rope = BM_ROPE_CENTER;
    }
}

static void bm_play_match(void)
{
    uint8_t row;
    uint8_t col;
    uint32_t start = game_millis();
    int32_t ai_wait_ms = 0;

    rope = BM_ROPE_CENTER;
    winner = GAME_PLAYER_NONE;
    game_input_reset();

    /* get ready: presses do not count yet */
    while (game_millis() - start < BM_COUNTDOWN_MS)
    {
        game_render_rope(bm_edge(), BM_COUNTDOWN_PERCENT);
        game_frame();
        game_input_reset();
    }

    ai_wait_ms = bm_mash_interval();
    while (!winner)
    {
        /* every new press counts for the owner of the half it is in */
        while (!winner && game_poll_press(&row, &col))
        {
            uint8_t side = bm_side_of(col);
            if (two_players || side != ai_side)
            {
                bm_pull(side);
            }
        }

        bm_drift();

        if (!two_players)
        {
            ai_wait_ms -= GAME_FRAME_MS;
            while (!winner && ai_wait_ms <= 0)
            {
                bm_pull(ai_side);
                ai_wait_ms += bm_mash_interval();
            }
        }

        bm_check_rope();
        game_render_rope(bm_edge(), 100);
        game_frame();
    }
}

void button_masher_run(void)
{
    uint8_t player = game_select_players();

    two_players = (player == GAME_PLAYER_NONE);
    if (!two_players)
    {
        ai_side = game_other_player(player);
        difficulty = game_select_difficulty();
    }
    bm_play_match();
    game_show_result(winner);
}
