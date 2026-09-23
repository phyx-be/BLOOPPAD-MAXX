/* Tug of war, ported from the BloopPad Maxx playground.
 *
 * Two halves, one rope: red owns the columns left of the rope, blue the columns right of it.
 * Every round, after a random wait, a white light appears in each half. The first to press
 * the light in their own half wins the round and pulls the rope one column towards the other side.
 * Pressing before the lights appear or pressing the wrong button loses the round.
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

#define TW_WAIT_MIN_MS    (900) /* the lights appear 0.9 .. 2.1 s after the start of a round */
#define TW_WAIT_RANDOM_MS (1200)
#define TW_RESULT_MS      (800) /* how long the winner of a round is shown */
#define TW_RESULT_PERCENT (55)  /* brightness of the half of the round winner */

typedef enum
{
    TW_WAIT,   /* waiting for the lights, pressing now loses the round */
    TW_GO,     /* the lights are on, press yours */
    TW_RESULT, /* show who won the round */
} tw_state_t;

/* average reaction time of the computer, per difficulty, as in the playground */
static const uint16_t reaction_ms[GAME_DIFFICULTY_COUNT] = {750, 420, 240};

static game_difficulty_t difficulty;
static uint8_t two_players;
static uint8_t ai_side;
static uint8_t rope; /* the column where blue's half starts */
static uint8_t winner;
static uint8_t round_winner;
static uint8_t targets[2][2]; /* {row, col} of the light of each player, indexed by player - 1 */
static tw_state_t state;
static uint32_t state_start_ms;
static uint32_t state_duration_ms;

static uint8_t tw_side_of(uint8_t col)
{
    return col < rope ? GAME_PLAYER_RED : GAME_PLAYER_BLUE;
}

static void tw_set_state(tw_state_t new_state, uint32_t duration_ms)
{
    state = new_state;
    state_start_ms = game_millis();
    state_duration_ms = duration_ms;
}

static void tw_wait(void)
{
    round_winner = GAME_PLAYER_NONE;
    tw_set_state(TW_WAIT, TW_WAIT_MIN_MS + game_random() % TW_WAIT_RANDOM_MS);
}

/* put a light on a random button in each half */
static void tw_go(void)
{
    uint8_t first_col[2] = {0, rope};
    uint8_t end_col[2] = {rope, GAME_COLS};

    for (int i = 0; i < 2; i++)
    {
        targets[i][0] = game_random() % GAME_ROWS;
        targets[i][1] = first_col[i] + game_random() % (end_col[i] - first_col[i]);
    }

    /* 80% .. 130% of the average reaction time */
    tw_set_state(TW_GO, (reaction_ms[difficulty] * (800 + game_random() % 500)) / 1000);
}

static void tw_end_round(uint8_t round_winner_side)
{
    round_winner = round_winner_side;
    if (round_winner == GAME_PLAYER_RED)
    {
        rope++;
    }
    else
    {
        rope--;
    }

    if (rope >= GAME_COLS)
    {
        winner = GAME_PLAYER_RED;
    }
    else if (rope == 0)
    {
        winner = GAME_PLAYER_BLUE;
    }
    tw_set_state(TW_RESULT, TW_RESULT_MS);
}

static void tw_press(uint8_t side, uint8_t row, uint8_t col)
{
    if (state == TW_WAIT)
    {
        /* jumped the gun */
        tw_end_round(game_other_player(side));
    }
    else if (state == TW_GO)
    {
        uint8_t hit = targets[side - 1][0] == row && targets[side - 1][1] == col;
        tw_end_round(hit ? side : game_other_player(side));
    }
}

static void tw_render(void)
{
    game_render_rope(rope, 100);

    if (state == TW_GO)
    {
        for (int i = 0; i < 2; i++)
        {
            game_set_led(targets[i][0], targets[i][1], game_white);
        }
    }

    if (state == TW_RESULT)
    {
        /* light up the half of the board of the round winner */
        uint8_t first_col = round_winner == GAME_PLAYER_RED ? 0 : GAME_COLS / 2;
        game_color_t color = game_scale_color(game_player_colors[round_winner - 1], TW_RESULT_PERCENT);

        for (int col = first_col; col < first_col + GAME_COLS / 2; col++)
        {
            for (int row = 0; row < GAME_ROWS; row++)
            {
                game_set_led(row, col, color);
            }
        }
    }
}

static void tw_play_match(void)
{
    uint8_t row;
    uint8_t col;

    rope = GAME_COLS / 2;
    winner = GAME_PLAYER_NONE;
    game_input_reset();
    tw_wait();

    while (!winner)
    {
        uint32_t elapsed_ms;

        /* a press counts for the owner of the half it is in */
        while (state != TW_RESULT && game_poll_press(&row, &col))
        {
            uint8_t side = tw_side_of(col);
            if (two_players || side != ai_side)
            {
                tw_press(side, row, col);
            }
        }

        /* after the presses: a press can start a new state */
        elapsed_ms = game_millis() - state_start_ms;
        if (state == TW_WAIT && elapsed_ms >= state_duration_ms)
        {
            tw_go();
        }
        else if (state == TW_GO && !two_players && elapsed_ms >= state_duration_ms)
        {
            /* the computer reacts */
            tw_press(ai_side, targets[ai_side - 1][0], targets[ai_side - 1][1]);
        }
        else if (state == TW_RESULT && elapsed_ms >= state_duration_ms)
        {
            /* presses during the result do not count */
            game_input_reset();
            tw_wait();
        }

        tw_render();
        game_frame();
    }
}

void tug_of_war_run(void)
{
    uint8_t player = game_select_players();

    two_players = (player == GAME_PLAYER_NONE);
    if (!two_players)
    {
        ai_side = game_other_player(player);
        difficulty = game_select_difficulty();
    }
    tw_play_match();
    game_show_result(winner);
}
