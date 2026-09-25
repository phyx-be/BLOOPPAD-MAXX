/* Battleships: sink the fleet of the other player first.
 *
 * At the start, pick the number of players with the red and blue buttons on the first row:
 *   press one of them:         one player with that color against the computer with the other color
 *   press both simultaneously: two players on one BloopPad Maxx
 * With one player, then pick the difficulty of the computer on the first row:
 *   col 0 (green):  easy
 *   col 1 (yellow): normal
 *   col 2 (red):    hard
 *
 * Every player gets a random fleet of 4 ships (lengths 4, 3, 3 and 2) that do not touch, not even
 * diagonally. The fleet is shown in the player's color: tap any button for a new fleet, hold a button
 * for 1 s to accept it. With two players, the name of the player scrolls by first, so the other player
 * can look away; press any button to show the fleet.
 *
 * Then the players take turns, a short flash in the color of the player shows whose turn it is.
 * The board shows the waters of the other player: tap an unknown cell to fire at it.
 *   dim dot in your color:     miss
 *   orange:                    hit
 *   the other player's color:  sunk ship; the water around it is marked as a miss, ships never touch
 * While the computer fires, the board shows your own fleet (dimmed) with its hits and misses.
 * When a fleet is sunk, the ships of the winner that were not hit are shown, then the winner scrolls by;
 * press any button to return to the game menu.
 */
#include <string.h> /* memset() */

#include "games.h"

#define BS_CELLS         (GAME_ROWS * GAME_COLS)
#define BS_SHIPS         (4)
#define BS_NO_SHIP       (0xff)
#define BS_HOLD_MS       (1000) /* hold a button this long to accept the fleet */
#define BS_TURN_MS       (500)  /* the flash in the color of the player whose turn it is */
#define BS_TURN_PERCENT  (25)
#define BS_THINK_MS      (600)  /* the computer "thinks" this long before it fires */
#define BS_AIM_MS        (450)  /* the target cell blinks this long before the result shows */
#define BS_BLINK_MS      (100)
#define BS_RESULT_MS     (900)  /* the result of a shot is shown this long */
#define BS_SUNK_MS       (1200) /* a sunk ship blinks this long */
#define BS_REVEAL_MS     (3000) /* the remaining ships are shown this long at the end */
#define BS_MISS_PERCENT  (15)
#define BS_FLEET_PERCENT (30) /* brightness of your own ships while the computer fires */
#define BS_HELD_MIN      (15) /* brightness (percent) of a held button when the hold starts */
#define BS_HIT_WEIGHT    (50) /* hard computer: how much more likely a ship is where it hit before */

/* the state of a cell of a fleet, as seen by the player that fires at it */
typedef enum
{
    BS_UNKNOWN = 0,
    BS_MISS,
    BS_HIT,
    BS_SUNK,  /* part of a sunk ship */
    BS_EMPTY, /* next to a sunk ship, so there is no ship; shown as a miss */
} bs_shot_t;

typedef enum
{
    BS_RESULT_MISS,
    BS_RESULT_HIT,
    BS_RESULT_SUNK,
} bs_result_t;

typedef struct
{
    uint8_t row;
    uint8_t col;
    uint8_t vertical;
    uint8_t hits;
} bs_ship_t;

typedef struct
{
    bs_ship_t ships[BS_SHIPS];
    uint8_t ship_at[BS_CELLS]; /* index in ships, or BS_NO_SHIP */
    uint8_t shots[BS_CELLS];   /* bs_shot_t, the shots fired at this fleet */
    uint8_t sunk;
} bs_fleet_t;

static const uint8_t ship_lengths[BS_SHIPS] = {4, 3, 3, 2};
static const game_color_t hit_color = {.r = 255, .g = 100, .b = 0};

static bs_fleet_t fleets[2]; /* indexed by player - 1 */
static game_difficulty_t difficulty;
static uint8_t two_players;
static uint8_t human; /* with one player */

static bs_fleet_t *fleet_of(uint8_t player)
{
    return &fleets[player - 1];
}

static game_color_t color_of(uint8_t player)
{
    return game_player_colors[player - 1];
}

/* the row and column of part i of a ship, returns its cell */
static uint8_t cell_of(const bs_ship_t *ship, uint8_t i, int8_t *row, int8_t *col)
{
    *row = ship->row + (ship->vertical ? i : 0);
    *col = ship->col + (ship->vertical ? 0 : i);
    return *row * GAME_COLS + *col;
}

static uint8_t any_held(const uint8_t *buttons)
{
    for (int row = 0; row < GAME_ROWS; row++)
    {
        if (buttons[row])
        {
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- the fleet */

/* 1 if a ship fits there and does not touch another ship, not even diagonally */
static uint8_t bs_free(const bs_fleet_t *fleet, int row, int col, uint8_t vertical, uint8_t length)
{
    int end_row = row + (vertical ? length - 1 : 0);
    int end_col = col + (vertical ? 0 : length - 1);

    if (end_row >= GAME_ROWS || end_col >= GAME_COLS)
    {
        return 0;
    }
    for (int r = row - 1; r <= end_row + 1; r++)
    {
        for (int c = col - 1; c <= end_col + 1; c++)
        {
            if (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && fleet->ship_at[r * GAME_COLS + c] != BS_NO_SHIP)
            {
                return 0;
            }
        }
    }
    return 1;
}

static void bs_random_fleet(bs_fleet_t *fleet)
{
    uint8_t placed;

    do
    {
        memset(fleet, 0, sizeof(*fleet));
        memset(fleet->ship_at, BS_NO_SHIP, sizeof(fleet->ship_at));
        placed = 0;

        /* the largest ship first; a crowded fleet starts over */
        for (int tries = 0; tries < 100 && placed < BS_SHIPS; tries++)
        {
            bs_ship_t *ship = &fleet->ships[placed];
            ship->vertical = game_random() & 1;
            ship->row = game_random() % GAME_ROWS;
            ship->col = game_random() % GAME_COLS;
            if (!bs_free(fleet, ship->row, ship->col, ship->vertical, ship_lengths[placed]))
            {
                continue;
            }

            for (int i = 0; i < ship_lengths[placed]; i++)
            {
                int8_t row;
                int8_t col;
                fleet->ship_at[cell_of(ship, i, &row, &col)] = placed;
            }
            placed++;
        }
    } while (placed < BS_SHIPS);
}

static bs_result_t bs_fire(bs_fleet_t *fleet, uint8_t cell)
{
    uint8_t index = fleet->ship_at[cell];

    if (index == BS_NO_SHIP)
    {
        fleet->shots[cell] = BS_MISS;
        return BS_RESULT_MISS;
    }

    bs_ship_t *ship = &fleet->ships[index];
    fleet->shots[cell] = BS_HIT;
    if (++ship->hits < ship_lengths[index])
    {
        return BS_RESULT_HIT;
    }

    /* sunk: mark the ship, and the water around it, where no other ship can be */
    fleet->sunk++;
    for (int i = 0; i < ship_lengths[index]; i++)
    {
        int8_t row;
        int8_t col;
        fleet->shots[cell_of(ship, i, &row, &col)] = BS_SUNK;
        for (int r = row - 1; r <= row + 1; r++)
        {
            for (int c = col - 1; c <= col + 1; c++)
            {
                if (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && fleet->shots[r * GAME_COLS + c] == BS_UNKNOWN)
                {
                    fleet->shots[r * GAME_COLS + c] = BS_EMPTY;
                }
            }
        }
    }
    return BS_RESULT_SUNK;
}

/* ---------------------------------------------------------------- the computer */

static uint8_t bs_has_open_hit(const bs_fleet_t *fleet)
{
    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        if (fleet->shots[cell] == BS_HIT)
        {
            return 1;
        }
    }
    return 0;
}

/* a random cell among the unknown cells for which pick[cell] is set, returns BS_CELLS if there is none */
static uint8_t bs_random_cell(const bs_fleet_t *fleet, const uint8_t *pick)
{
    uint8_t count = 0;

    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        count += fleet->shots[cell] == BS_UNKNOWN && pick[cell];
    }
    if (!count)
    {
        return BS_CELLS;
    }

    uint8_t n = game_random() % count;
    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        if (fleet->shots[cell] == BS_UNKNOWN && pick[cell] && n-- == 0)
        {
            return cell;
        }
    }
    return BS_CELLS;
}

/* the most likely cell of a ship: count the ways the ships that are left can be placed over every cell;
 * a placement over hits is much more likely, a placement over a miss or a sunk ship is impossible
 */
static uint8_t bs_most_likely_cell(const bs_fleet_t *fleet)
{
    uint16_t weight[BS_CELLS];
    uint16_t best = 0;
    uint8_t best_cells[BS_CELLS];

    memset(weight, 0, sizeof(weight));
    for (int index = 0; index < BS_SHIPS; index++)
    {
        uint8_t length = ship_lengths[index];
        if (fleet->ships[index].hits == length)
        {
            continue; /* sunk */
        }

        for (int vertical = 0; vertical <= 1; vertical++)
        {
            for (int row = 0; row + (vertical ? length : 1) <= GAME_ROWS; row++)
            {
                for (int col = 0; col + (vertical ? 1 : length) <= GAME_COLS; col++)
                {
                    uint8_t hits = 0;
                    uint8_t possible = 1;
                    for (int i = 0; i < length && possible; i++)
                    {
                        uint8_t shot = fleet->shots[(row + (vertical ? i : 0)) * GAME_COLS + col + (vertical ? 0 : i)];
                        hits += shot == BS_HIT;
                        possible = shot == BS_UNKNOWN || shot == BS_HIT;
                    }
                    if (!possible)
                    {
                        continue;
                    }
                    for (int i = 0; i < length; i++)
                    {
                        weight[(row + (vertical ? i : 0)) * GAME_COLS + col + (vertical ? 0 : i)] += 1 + BS_HIT_WEIGHT * hits;
                    }
                }
            }
        }
    }

    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        uint8_t unknown = fleet->shots[cell] == BS_UNKNOWN;
        best_cells[cell] = unknown && weight[cell] == best;
        if (unknown && weight[cell] > best)
        {
            best = weight[cell];
            memset(best_cells, 0, cell);
            best_cells[cell] = 1;
        }
    }
    return bs_random_cell(fleet, best_cells);
}

static uint8_t bs_computer_shot(const bs_fleet_t *fleet)
{
    uint8_t pick[BS_CELLS];
    uint8_t cell;

    if (difficulty == GAME_DIFFICULTY_HARD || (difficulty == GAME_DIFFICULTY_NORMAL && bs_has_open_hit(fleet)))
    {
        cell = bs_most_likely_cell(fleet);
    }
    else
    {
        memset(pick, 0, sizeof(pick));
        for (int row = 0; row < GAME_ROWS; row++)
        {
            for (int col = 0; col < GAME_COLS; col++)
            {
                if (difficulty == GAME_DIFFICULTY_NORMAL)
                {
                    /* every ship covers a cell of the checkerboard */
                    pick[row * GAME_COLS + col] = (row + col) % 2 == 0;
                }
                else if (bs_has_open_hit(fleet))
                {
                    /* easy: next to a hit */
                    static const int8_t next[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (int i = 0; i < 4; i++)
                    {
                        int r = row + next[i][0];
                        int c = col + next[i][1];
                        if (r >= 0 && r < GAME_ROWS && c >= 0 && c < GAME_COLS && fleet->shots[r * GAME_COLS + c] == BS_HIT)
                        {
                            pick[row * GAME_COLS + col] = 1;
                        }
                    }
                }
                else
                {
                    pick[row * GAME_COLS + col] = 1;
                }
            }
        }
        cell = bs_random_cell(fleet, pick);
    }

    if (cell == BS_CELLS)
    {
        /* nothing left to pick from, any unknown cell will do */
        memset(pick, 1, sizeof(pick));
        cell = bs_random_cell(fleet, pick);
    }
    return cell;
}

/* ---------------------------------------------------------------- drawing */

/* the waters of target as seen by shooter; reveal also shows the ships that were not hit */
static void bs_render_target(uint8_t shooter, uint8_t target, uint8_t reveal)
{
    const bs_fleet_t *fleet = fleet_of(target);

    board_clear_leds();
    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        uint8_t row = cell / GAME_COLS;
        uint8_t col = cell % GAME_COLS;
        switch (fleet->shots[cell])
        {
            case BS_MISS:
            case BS_EMPTY:
                game_set_led(row, col, game_scale_color(color_of(shooter), BS_MISS_PERCENT));
                break;
            case BS_HIT:
                game_set_led(row, col, hit_color);
                break;
            case BS_SUNK:
                game_set_led(row, col, color_of(target));
                break;
            default:
                if (reveal && fleet->ship_at[cell] != BS_NO_SHIP)
                {
                    game_set_led(row, col, game_scale_color(color_of(target), BS_FLEET_PERCENT));
                }
                break;
        }
    }
}

/* the fleet of owner with the shots fired at it; percent is the brightness of the ships */
static void bs_render_fleet(uint8_t owner, uint8_t percent)
{
    const bs_fleet_t *fleet = fleet_of(owner);

    board_clear_leds();
    for (int cell = 0; cell < BS_CELLS; cell++)
    {
        uint8_t row = cell / GAME_COLS;
        uint8_t col = cell % GAME_COLS;
        uint8_t shot = fleet->shots[cell];
        if (shot == BS_HIT || shot == BS_SUNK)
        {
            game_set_led(row, col, hit_color);
        }
        else if (fleet->ship_at[cell] != BS_NO_SHIP)
        {
            game_set_led(row, col, game_scale_color(color_of(owner), percent));
        }
        else if (shot != BS_UNKNOWN)
        {
            game_set_led(row, col, game_scale_color(game_white, BS_MISS_PERCENT));
        }
    }
}

/* draw the view of a shot: the target waters when a human fires, the own fleet when the computer does */
static void bs_render_view(uint8_t shooter, uint8_t target)
{
    if (!two_players && shooter != human)
    {
        bs_render_fleet(target, BS_FLEET_PERCENT);
    }
    else
    {
        bs_render_target(shooter, target, 0);
    }
}

/* fill the board with the color of the player whose turn it is */
static void bs_show_turn(uint8_t player)
{
    uint32_t start = game_millis();

    while (game_millis() - start < BS_TURN_MS)
    {
        for (int row = 0; row < GAME_ROWS; row++)
        {
            for (int col = 0; col < GAME_COLS; col++)
            {
                game_set_led(row, col, game_scale_color(color_of(player), BS_TURN_PERCENT));
            }
        }
        game_frame();
    }
}

/* ---------------------------------------------------------------- the screens */

/* scroll the name of the player until a button is pressed, so the other player can look away */
static void bs_pass_to(uint8_t player)
{
    uint8_t row;
    uint8_t col;
    uint32_t start = game_millis();

    game_input_reset();
    while (!game_poll_press(&row, &col))
    {
        game_render_text(player == GAME_PLAYER_RED ? "RED" : "BLUE", game_millis() - start, color_of(player));
        game_frame();
    }
}

/* show a random fleet: a tap shuffles it, a hold accepts it */
static void bs_choose_fleet(uint8_t player)
{
    uint8_t buttons[GAME_ROWS];
    uint8_t held_row = 0;
    uint8_t held_col = 0;
    uint8_t holding = 0;
    uint32_t hold_start = 0;

    bs_random_fleet(fleet_of(player));

    /* the button that opened this screen only counts after a release */
    do
    {
        board_read_buttons(buttons);
        bs_render_fleet(player, 100);
        game_frame();
    } while (any_held(buttons));

    while (1)
    {
        board_read_buttons(buttons);
        if (!holding)
        {
            for (int row = 0; row < GAME_ROWS && !holding; row++)
            {
                for (int col = 0; col < GAME_COLS && !holding; col++)
                {
                    if (buttons[row] & GAME_BUTTON(col))
                    {
                        holding = 1;
                        held_row = row;
                        held_col = col;
                        hold_start = game_millis();
                    }
                }
            }
        }
        else if (!(buttons[held_row] & GAME_BUTTON(held_col)))
        {
            /* a tap: a new fleet; the moment of the tap is the best source of randomness we have */
            holding = 0;
            game_seed_random(game_millis() * 2654435761u);
            bs_random_fleet(fleet_of(player));
        }
        else if (game_millis() - hold_start >= BS_HOLD_MS)
        {
            game_input_reset();
            return;
        }

        bs_render_fleet(player, 100);
        if (holding)
        {
            uint32_t held_ms = game_millis() - hold_start;
            game_set_led(held_row, held_col, game_scale_color(game_white, BS_HELD_MIN + ((100 - BS_HELD_MIN) * held_ms) / BS_HOLD_MS));
        }
        game_frame();
    }
}

/* wait for the human to tap an unknown cell of the target */
static uint8_t bs_human_shot(uint8_t shooter, uint8_t target)
{
    uint8_t row;
    uint8_t col;

    game_input_reset();
    while (1)
    {
        bs_render_target(shooter, target, 0);
        game_frame();
        if (game_poll_press(&row, &col) && fleet_of(target)->shots[row * GAME_COLS + col] == BS_UNKNOWN)
        {
            return row * GAME_COLS + col;
        }
    }
}

/* blink the cell, fire at it and show the result */
static bs_result_t bs_shoot(uint8_t shooter, uint8_t target, uint8_t cell)
{
    bs_fleet_t *fleet = fleet_of(target);
    uint8_t row = cell / GAME_COLS;
    uint8_t col = cell % GAME_COLS;
    uint32_t start = game_millis();

    while (game_millis() - start < BS_AIM_MS)
    {
        bs_render_view(shooter, target);
        if (((game_millis() - start) / BS_BLINK_MS) % 2 == 0)
        {
            game_set_led(row, col, game_white);
        }
        game_frame();
    }

    bs_result_t result = bs_fire(fleet, cell);
    uint8_t index = fleet->ship_at[cell];
    uint32_t duration = result == BS_RESULT_SUNK ? BS_SUNK_MS : BS_RESULT_MS;

    start = game_millis();
    while (game_millis() - start < duration)
    {
        bs_render_view(shooter, target);
        if (result == BS_RESULT_SUNK && ((game_millis() - start) / (2 * BS_BLINK_MS)) % 2)
        {
            /* blink the sunk ship */
            for (int i = 0; i < ship_lengths[index]; i++)
            {
                int8_t r;
                int8_t c;
                cell_of(&fleet->ships[index], i, &r, &c);
                game_set_led(r, c, game_scale_color(color_of(target), BS_FLEET_PERCENT));
            }
        }
        game_frame();
    }
    return result;
}

void battleships_run(void)
{
    uint8_t single = game_select_players();

    two_players = single == GAME_PLAYER_NONE;
    human = single;
    if (!two_players)
    {
        difficulty = game_select_difficulty();
    }

    if (two_players)
    {
        for (uint8_t player = GAME_PLAYER_RED; player <= GAME_PLAYER_BLUE; player++)
        {
            bs_pass_to(player);
            bs_choose_fleet(player);
        }
    }
    else
    {
        bs_random_fleet(fleet_of(game_other_player(human)));
        bs_choose_fleet(human);
    }

    /* red starts with two players, the human with one */
    uint8_t shooter = two_players ? GAME_PLAYER_RED : human;
    while (1)
    {
        uint8_t target = game_other_player(shooter);

        bs_show_turn(shooter);
        uint8_t cell;
        if (two_players || shooter == human)
        {
            cell = bs_human_shot(shooter, target);
        }
        else
        {
            uint32_t start = game_millis();
            while (game_millis() - start < BS_THINK_MS)
            {
                bs_render_fleet(target, BS_FLEET_PERCENT);
                game_frame();
            }
            cell = bs_computer_shot(fleet_of(target));
        }

        bs_shoot(shooter, target, cell);
        if (fleet_of(target)->sunk == BS_SHIPS)
        {
            break;
        }
        shooter = target;
    }

    /* show the ships of the winner that were not sunk, as seen by the loser */
    uint32_t start = game_millis();
    while (game_millis() - start < BS_REVEAL_MS)
    {
        bs_render_target(game_other_player(shooter), shooter, 1);
        game_frame();
    }

    game_show_result(shooter);
}
