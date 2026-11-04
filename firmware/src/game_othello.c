/* Othello (Reversi): trap the opponent's discs between yours to flip them.
 * Most discs on the board when it fills up (or nobody can move) wins.
 */
#include <string.h> /* memcpy() */

#include "debug.h"
#include "games.h"

#define OTHELLO_EMPTY   (0)
#define OTHELLO_PASS_MS (1200) /* how long a pass is shown */

/* the 8 directions to look for discs to flip */
static const int8_t dirs[8][2] = {
    {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1},
};

/* positional value of every cell, corners are the best, the cells next to a
 * corner the worst - used by the hard computer player
 */
static const int16_t cell_value[GAME_ROWS][GAME_COLS] = {
    {120, -20, 20, 5, 5, 20, -20, 120},
    {-20, -40, -5, -5, -5, -5, -40, -20},
    {20, -5, 15, 3, 3, 15, -5, 20},
    {5, -5, 3, 3, 3, 3, -5, 5},
    {5, -5, 3, 3, 3, 3, -5, 5},
    {20, -5, 15, 3, 3, 15, -5, 20},
    {-20, -40, -5, -5, -5, -5, -40, -20},
    {120, -20, 20, 5, 5, 20, -20, 120},
};

/* how many discs flip if player plays (row, col), 0 if the move is not legal */
static uint8_t count_flips(uint8_t board[GAME_ROWS][GAME_COLS], uint8_t row, uint8_t col, uint8_t player)
{
    uint8_t other = game_other_player(player);
    uint8_t total = 0;

    if (board[row][col] != OTHELLO_EMPTY)
    {
        return 0;
    }

    for (int d = 0; d < 8; d++)
    {
        uint8_t flips = 0;
        int r = row + dirs[d][0];
        int c = col + dirs[d][1];

        while (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && board[r][c] == other)
        {
            flips++;
            r += dirs[d][0];
            c += dirs[d][1];
        }

        if (flips > 0 && r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && board[r][c] == player)
        {
            total += flips;
        }
    }
    return total;
}

static uint8_t has_any_move(uint8_t board[GAME_ROWS][GAME_COLS], uint8_t player)
{
    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            if (count_flips(board, r, c, player) > 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

static void apply_move(uint8_t board[GAME_ROWS][GAME_COLS], uint8_t row, uint8_t col, uint8_t player)
{
    uint8_t other = game_other_player(player);

    board[row][col] = player;
    for (int d = 0; d < 8; d++)
    {
        uint8_t flips = 0;
        int r = row + dirs[d][0];
        int c = col + dirs[d][1];

        while (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && board[r][c] == other)
        {
            flips++;
            r += dirs[d][0];
            c += dirs[d][1];
        }

        if (flips > 0 && r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && board[r][c] == player)
        {
            r = row + dirs[d][0];
            c = col + dirs[d][1];
            while (board[r][c] == other)
            {
                board[r][c] = player;
                r += dirs[d][0];
                c += dirs[d][1];
            }
        }
    }
}

/* the computer picks a move: easy plays a random legal move, normal maximizes
 * the number of flips, hard weighs the position and the opponent's best reply
 */
static void computer_move(uint8_t board[GAME_ROWS][GAME_COLS], uint8_t player, game_difficulty_t difficulty)
{
    int16_t best_score = -0x4000;
    uint8_t best_row = 0;
    uint8_t best_col = 0;

    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            uint8_t board_after[GAME_ROWS][GAME_COLS];
            uint8_t flips = count_flips(board, r, c, player);
            int16_t score;

            if (flips == 0)
            {
                continue;
            }

            if (difficulty == GAME_DIFFICULTY_EASY)
            {
                /* every legal move has an equal chance, the last one wins ties */
                score = (int16_t)(game_random() % 100);
            }
            else if (difficulty == GAME_DIFFICULTY_NORMAL)
            {
                score = (int16_t)flips * 10 + (int16_t)(game_random() % 5);
            }
            else
            {
                /* hard: position plus flips, minus the best counter the opponent has */
                int16_t best_reply = 0;
                memcpy(board_after, board, sizeof(board_after));
                apply_move(board_after, r, c, player);

                for (int rr = 0; rr < GAME_ROWS; rr++)
                {
                    for (int cc = 0; cc < GAME_COLS; cc++)
                    {
                        uint8_t reply_flips = count_flips(board_after, rr, cc, game_other_player(player));
                        if (reply_flips > 0)
                        {
                            int16_t reply = (int16_t)reply_flips * 8 + cell_value[rr][cc];
                            if (reply > best_reply)
                            {
                                best_reply = reply;
                            }
                        }
                    }
                }
                score = cell_value[r][c] + (int16_t)flips * 4 - best_reply;
            }

            if (score > best_score)
            {
                best_score = score;
                best_row = r;
                best_col = c;
            }
        }
    }

    apply_move(board, best_row, best_col, player);
}

/* flash the discs that were just flipped white for a moment */
static void show_flips(void)
{
    for (int f = 0; f < 3; f++)
    {
        game_frame();
    }
}

void othello_run(void)
{
    uint8_t board[GAME_ROWS][GAME_COLS] = {0};
    uint8_t single_player;
    uint8_t computer = GAME_PLAYER_NONE;
    game_difficulty_t difficulty = GAME_DIFFICULTY_NORMAL;
    uint8_t current = GAME_PLAYER_RED;
    uint8_t row;
    uint8_t col;
    uint32_t pass_start;
    uint8_t winner = GAME_PLAYER_DRAW;
    uint8_t red_discs = 0;
    uint8_t blue_discs = 0;

    single_player = game_select_players();
    if (single_player != GAME_PLAYER_NONE)
    {
        computer = game_other_player(single_player);
        difficulty = game_select_difficulty();
    }

    /* the standard starting position: a 2x2 checkerboard in the middle */
    board[3][3] = GAME_PLAYER_BLUE;
    board[3][4] = GAME_PLAYER_RED;
    board[4][3] = GAME_PLAYER_RED;
    board[4][4] = GAME_PLAYER_BLUE;

    game_input_reset();
    while (1)
    {
        /* when the player to move cannot move, pass or end the game */
        if (!has_any_move(board, current))
        {
            if (!has_any_move(board, game_other_player(current)))
            {
                break; /* nobody can move: the game is over */
            }

            pass_start = game_millis();
            while (game_millis() - pass_start < OTHELLO_PASS_MS)
            {
                game_render_text(current == GAME_PLAYER_RED ? "RED PASSES" : "BLUE PASSES", game_millis() - pass_start, game_player_colors[current - 1]);
                game_frame();
            }
            current = game_other_player(current);
            continue;
        }

        /* draw the board, the legal moves of the player to move are dim */
        board_clear_leds();
        for (int r = 0; r < GAME_ROWS; r++)
        {
            for (int c = 0; c < GAME_COLS; c++)
            {
                if (board[r][c] != OTHELLO_EMPTY)
                {
                    game_set_led(r, c, game_player_colors[board[r][c] - 1]);
                }
                else if (current != computer && count_flips(board, r, c, current) > 0)
                {
                    game_set_led(r, c, game_scale_color(game_player_colors[current - 1], 25));
                }
            }
        }
        game_frame();

        /* the computer moves without waiting for a press */
        if (current == computer)
        {
            computer_move(board, current, difficulty);
            show_flips();
            current = game_other_player(current);
            game_input_reset();
            continue;
        }

        if (!game_poll_press(&row, &col))
        {
            continue;
        }

        if (count_flips(board, row, col, current) > 0)
        {
            PRINT("othello: %s plays %u,%u\r\n", current == GAME_PLAYER_RED ? "red" : "blue", row, col);
            apply_move(board, row, col, current);
            show_flips();
            current = game_other_player(current);
            game_input_reset(); /* a press only counts once */
        }
    }

    /* count the discs to find the winner */
    for (int r = 0; r < GAME_ROWS; r++)
    {
        for (int c = 0; c < GAME_COLS; c++)
        {
            if (board[r][c] == GAME_PLAYER_RED)
            {
                red_discs++;
            }
            else if (board[r][c] == GAME_PLAYER_BLUE)
            {
                blue_discs++;
            }
        }
    }
    if (red_discs > blue_discs)
    {
        winner = GAME_PLAYER_RED;
    }
    else if (blue_discs > red_discs)
    {
        winner = GAME_PLAYER_BLUE;
    }

    /* show the final board for a moment before the result scrolls by */
    pass_start = game_millis();
    while (game_millis() - pass_start < 2000)
    {
        board_clear_leds();
        for (int r = 0; r < GAME_ROWS; r++)
        {
            for (int c = 0; c < GAME_COLS; c++)
            {
                if (board[r][c] != OTHELLO_EMPTY)
                {
                    game_set_led(r, c, game_player_colors[board[r][c] - 1]);
                }
            }
        }
        game_frame();
    }

    game_show_result(winner);
}
