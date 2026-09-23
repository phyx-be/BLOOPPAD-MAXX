/* Connect Four, ported from the BloopPad Maxx playground.
 *
 * With two players, red (player 1) starts; with one player, the player always starts.
 * Pressing any button in a column drops a piece in that column.
 * At the start, pick the number of players with the red and blue buttons on the first row:
 *   press one of them:         one player with that color against the computer with the other color
 *   press both simultaneously: two players on one BloopPad Maxx
 * With one player, then pick the difficulty of the computer on the first row:
 *   col 0 (green):  easy
 *   col 1 (yellow): normal
 *   col 2 (red):    hard
 * After a game, the result scrolls by; press any button to return to the game menu.
 */
#include <string.h> /* memset() */

#include "games.h"

#define C4_CELLS          (GAME_ROWS * GAME_COLS)
#define C4_EMPTY          GAME_PLAYER_NONE
#define C4_RED            GAME_PLAYER_RED
#define C4_BLUE           GAME_PLAYER_BLUE
#define C4_DRAW           GAME_PLAYER_DRAW
#define C4_NO_MOVE        (0xff)
#define C4_AI_DELAY_MS    (450)  /* pause before the computer drops its piece */
#define C4_WIN_LINE_MS    (3000) /* how long the winning line is shown before the result text */
#define C4_PULSE_MS       (900)  /* period of the pulsing last move */
#define C4_EASY_RANDOM    (40)   /* percentage of random moves in easy mode */
#define C4_SCORE_WIN      (100000)
#define C4_SCORE_ROOT_WIN (1000000)
#define C4_INFINITY       (0x7fffffff)

/* search depth per difficulty, as in the playground */
static const uint8_t search_depth[GAME_DIFFICULTY_COUNT] = {1, 3, 5};

/* columns ordered from the center outwards: central moves are usually best, which speeds up the search */
static const uint8_t column_order[GAME_COLS] = {3, 4, 2, 5, 1, 6, 0, 7};

/* line directions as {dy, dx}: horizontal, vertical and both diagonals */
static const int8_t directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

/* score of a window of 4 cells, by the number of pieces of one player in it */
static const int32_t window_scores[5] = {0, 1, 12, 90, 0};

/* board cells, row major, row 0 is the top */
static uint8_t cells[C4_CELLS];
static game_difficulty_t difficulty;
static uint8_t two_players;
static uint8_t ai_side;
static uint8_t turn;
static uint8_t winner;
static uint8_t last_move;
static uint8_t win_line[GAME_COLS];
static uint8_t win_line_len;
static uint32_t turn_start_ms;

static uint8_t other(uint8_t player)
{
    return game_other_player(player);
}

/* the cell a piece dropped in this column lands on, -1 if the column is full */
static int8_t c4_drop(const uint8_t *board, uint8_t col)
{
    for (int row = GAME_ROWS - 1; row >= 0; row--)
    {
        if (board[row * GAME_COLS + col] == C4_EMPTY)
        {
            return row * GAME_COLS + col;
        }
    }
    return -1;
}

/* length of a line of 4 or more through this cell, 0 if there is none; line receives its cells */
static uint8_t c4_line(const uint8_t *board, uint8_t index, uint8_t *line)
{
    uint8_t player = board[index];
    int8_t row = index / GAME_COLS;
    int8_t col = index % GAME_COLS;

    if (player == C4_EMPTY)
    {
        return 0;
    }

    for (int d = 0; d < 4; d++)
    {
        uint8_t cells_in_line[GAME_COLS];
        uint8_t len = 0;

        cells_in_line[len++] = index;
        for (int sign = 1; sign >= -1; sign -= 2)
        {
            int8_t y = row + directions[d][0] * sign;
            int8_t x = col + directions[d][1] * sign;
            while (y >= 0 && y < GAME_ROWS && x >= 0 && x < GAME_COLS && board[y * GAME_COLS + x] == player)
            {
                cells_in_line[len++] = y * GAME_COLS + x;
                y += directions[d][0] * sign;
                x += directions[d][1] * sign;
            }
        }

        if (len >= 4)
        {
            if (line)
            {
                memcpy(line, cells_in_line, len);
            }
            return len;
        }
    }
    return 0;
}

/* heuristic score of the board for this player: open windows of 4 with more own pieces score higher */
static int32_t c4_evaluate(const uint8_t *board, uint8_t player)
{
    int32_t score = 0;

    for (int y = 0; y < GAME_ROWS; y++)
    {
        for (int x = 0; x < GAME_COLS; x++)
        {
            for (int d = 0; d < 4; d++)
            {
                int end_y = y + 3 * directions[d][0];
                int end_x = x + 3 * directions[d][1];
                uint8_t mine = 0;
                uint8_t theirs = 0;

                if (end_y < 0 || end_y >= GAME_ROWS || end_x < 0 || end_x >= GAME_COLS)
                {
                    continue;
                }

                for (int k = 0; k < 4; k++)
                {
                    uint8_t value = board[(y + k * directions[d][0]) * GAME_COLS + x + k * directions[d][1]];
                    if (value == player)
                    {
                        mine++;
                    }
                    else if (value != C4_EMPTY)
                    {
                        theirs++;
                    }
                }

                if (mine && theirs)
                {
                    continue;
                }
                score += window_scores[mine] - window_scores[theirs];
            }
        }
    }
    return score;
}

static int32_t c4_negamax(uint8_t *board, uint8_t player, uint8_t depth, int32_t alpha, int32_t beta)
{
    int32_t best = -C4_INFINITY;
    uint8_t any_move = 0;

    for (int i = 0; i < GAME_COLS; i++)
    {
        int8_t index = c4_drop(board, column_order[i]);
        int32_t value;

        if (index < 0)
        {
            continue;
        }
        if (depth == 0)
        {
            return c4_evaluate(board, player);
        }
        any_move = 1;

        board[index] = player;
        if (c4_line(board, index, NULL))
        {
            value = C4_SCORE_WIN + depth;
        }
        else
        {
            value = -c4_negamax(board, other(player), depth - 1, -beta, -alpha);
        }
        board[index] = C4_EMPTY;

        if (value > best)
        {
            best = value;
        }
        if (best > alpha)
        {
            alpha = best;
        }
        if (alpha >= beta)
        {
            break;
        }
    }

    /* a full board is a draw */
    return any_move ? best : 0;
}

/* pick the computer's column, ties are broken randomly */
static int8_t c4_best_column(uint8_t player)
{
    static uint8_t working[C4_CELLS];
    uint8_t choices[GAME_COLS];
    uint8_t choice_count = 0;
    int32_t best = -C4_INFINITY;
    uint8_t depth = search_depth[difficulty];

    for (int i = 0; i < GAME_COLS; i++)
    {
        if (c4_drop(cells, column_order[i]) >= 0)
        {
            choices[choice_count++] = column_order[i];
        }
    }
    if (choice_count == 0)
    {
        return -1;
    }
    if (difficulty == GAME_DIFFICULTY_EASY && (game_random() % 100) < C4_EASY_RANDOM)
    {
        return choices[game_random() % choice_count];
    }

    memcpy(working, cells, C4_CELLS);
    choice_count = 0;
    for (int i = 0; i < GAME_COLS; i++)
    {
        uint8_t col = column_order[i];
        int8_t index = c4_drop(working, col);
        int32_t value;

        if (index < 0)
        {
            continue;
        }

        working[index] = player;
        if (c4_line(working, index, NULL))
        {
            value = C4_SCORE_ROOT_WIN;
        }
        else
        {
            value = -c4_negamax(working, other(player), depth - 1, -C4_INFINITY, C4_INFINITY);
        }
        working[index] = C4_EMPTY;

        if (value > best)
        {
            best = value;
            choice_count = 0;
        }
        if (value == best)
        {
            choices[choice_count++] = col;
        }
    }
    return choices[game_random() % choice_count];
}

static uint8_t c4_ai_turn(void)
{
    return !two_players && turn == ai_side;
}

static void c4_play(uint8_t col)
{
    int8_t index = c4_drop(cells, col);

    if (index < 0 || winner)
    {
        return;
    }

    cells[index] = turn;
    last_move = index;

    win_line_len = c4_line(cells, index, win_line);
    if (win_line_len)
    {
        winner = turn;
        return;
    }

    winner = C4_DRAW;
    for (int i = 0; i < C4_CELLS; i++)
    {
        if (cells[i] == C4_EMPTY)
        {
            winner = 0;
            break;
        }
    }

    turn = other(turn);
    turn_start_ms = game_millis();
}

static void c4_render(void)
{
    board_clear_leds();

    for (int i = 0; i < C4_CELLS; i++)
    {
        if (cells[i] != C4_EMPTY)
        {
            game_set_led(i / GAME_COLS, i % GAME_COLS, game_player_colors[cells[i] - 1]);
        }
    }

    /* the last move pulses towards white */
    if (last_move != C4_NO_MOVE)
    {
        game_color_t color = game_player_colors[cells[last_move] - 1];
        uint32_t phase = game_millis() % C4_PULSE_MS;
        uint32_t pulse = phase < C4_PULSE_MS / 2 ? phase : C4_PULSE_MS - phase; /* 0 .. C4_PULSE_MS / 2 */

        color.r += ((255 - color.r) * pulse) / C4_PULSE_MS;
        color.g += ((255 - color.g) * pulse) / C4_PULSE_MS;
        color.b += ((255 - color.b) * pulse) / C4_PULSE_MS;
        game_set_led(last_move / GAME_COLS, last_move % GAME_COLS, color);
    }

    for (int i = 0; i < win_line_len; i++)
    {
        game_set_led(win_line[i] / GAME_COLS, win_line[i] % GAME_COLS, game_white);
    }
}

static void c4_new_game(void)
{
    memset(cells, C4_EMPTY, C4_CELLS);
    turn = two_players ? C4_RED : other(ai_side);
    winner = 0;
    last_move = C4_NO_MOVE;
    win_line_len = 0;
    turn_start_ms = game_millis();
    game_input_reset();
}

static void c4_play_game(void)
{
    uint8_t row;
    uint8_t col;

    c4_new_game();
    while (!winner)
    {
        if (c4_ai_turn())
        {
            /* ignore presses while the computer is thinking */
            while (game_poll_press(&row, &col))
            {
            }
            if (game_millis() - turn_start_ms >= C4_AI_DELAY_MS)
            {
                int8_t best = c4_best_column(turn);
                if (best >= 0)
                {
                    c4_play(best);
                }
            }
        }
        else if (game_poll_press(&row, &col))
        {
            c4_play(col);
        }

        c4_render();
        game_frame();
    }
}

static void c4_show_result(void)
{
    uint32_t start = game_millis();

    /* show the final board with the winning line */
    while (game_millis() - start < C4_WIN_LINE_MS)
    {
        c4_render();
        game_frame();
    }

    game_show_result(winner);
}

void connect_four_run(void)
{
    uint8_t player = game_select_players();

    two_players = (player == C4_EMPTY);
    if (!two_players)
    {
        ai_side = other(player);
        difficulty = game_select_difficulty();
    }
    c4_play_game();
    c4_show_result();
}
