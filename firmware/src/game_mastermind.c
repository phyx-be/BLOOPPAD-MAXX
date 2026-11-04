/* Mastermind: crack a secret code of 4 colored pegs (6 colors) in 7 guesses.
 * The current guess is built on the bottom row: press a peg slot to cycle its
 * color, press the green enter button to submit. The feedback on the right of
 * each stored guess: bright white = right color in the right place, dim white
 * = right color in the wrong place.
 */
#include <string.h> /* memset() */

#include "debug.h"
#include "games.h"

#define MM_PEGS        (4) /* code length, in columns 0..3 */
#define MM_COLORS      (6) /* number of peg colors */
#define MM_GUESSES     (7) /* rows 6 down to 0 */
#define MM_ENTER_COL   (7) /* the enter button on the entry row */
#define MM_ENTRY_ROW   (GAME_ROWS - 1)

static const game_color_t mm_peg_colors[MM_COLORS] = {
    {.r = 255, .g = 0, .b = 0},     /* 0: red */
    {.r = 0, .g = 255, .b = 0},     /* 1: green */
    {.r = 0, .g = 80, .b = 255},    /* 2: blue */
    {.r = 255, .g = 255, .b = 0},   /* 3: yellow */
    {.r = 0, .g = 255, .b = 255},   /* 4: cyan */
    {.r = 255, .g = 0, .b = 255},   /* 5: magenta */
};

static const game_color_t mm_exact_color = {.r = 255, .g = 255, .b = 255}; /* right color, right place */
static const game_color_t mm_partial_color = {.r = 70, .g = 70, .b = 70};  /* right color, wrong place */
static const game_color_t mm_enter_color = {.r = 0, .g = 255, .b = 0};     /* the submit button */
static const game_color_t mm_lose_color = {.r = 255, .g = 0, .b = 0};

typedef struct
{
    uint8_t code[MM_PEGS];                                   /* the secret code */
    uint8_t guesses[MM_GUESSES][MM_PEGS];                    /* the submitted guesses */
    uint8_t exact[MM_GUESSES];                               /* feedback: right color and place */
    uint8_t partial[MM_GUESSES];                             /* feedback: right color, wrong place */
    uint8_t guess_count;
} mm_state_t;

/* compute the feedback for a guess against the code */
static void mm_score(const mm_state_t *state, const uint8_t guess[MM_PEGS], uint8_t *exact, uint8_t *partial)
{
    uint8_t code_left[MM_PEGS];
    uint8_t guess_left[MM_PEGS];
    uint8_t n_left = 0;

    *exact = 0;
    for (int i = 0; i < MM_PEGS; i++)
    {
        if (guess[i] == state->code[i])
        {
            (*exact)++;
        }
        else
        {
            code_left[n_left] = state->code[i];
            guess_left[n_left] = guess[i];
            n_left++;
        }
    }

    *partial = 0;
    for (int i = 0; i < n_left; i++)
    {
        for (int j = 0; j < n_left; j++)
        {
            if (guess_left[i] == code_left[j])
            {
                (*partial)++;
                code_left[j] = 0xff; /* each code peg matches at most one guess peg */
                break;
            }
        }
    }
}

static void mm_render(const mm_state_t *state, const uint8_t entry[MM_PEGS])
{
    board_clear_leds();

    /* the stored guesses, the newest closest to the entry row */
    for (int g = 0; g < state->guess_count; g++)
    {
        uint8_t r = GAME_ROWS - 2 - g;
        for (int p = 0; p < MM_PEGS; p++)
        {
            game_set_led(r, p, mm_peg_colors[state->guesses[g][p]]);
        }
        /* the feedback pegs in columns 4..7: exact first, then partial */
        for (int f = 0; f < MM_PEGS; f++)
        {
            if (f < state->exact[g])
            {
                game_set_led(r, MM_PEGS + f, mm_exact_color);
            }
            else if (f < state->exact[g] + state->partial[g])
            {
                game_set_led(r, MM_PEGS + f, mm_partial_color);
            }
        }
    }

    /* the entry row with the pegs being built and the enter button */
    for (int p = 0; p < MM_PEGS; p++)
    {
        game_set_led(MM_ENTRY_ROW, p, mm_peg_colors[entry[p]]);
    }
    game_set_led(MM_ENTRY_ROW, MM_ENTER_COL, mm_enter_color);
}

/* let a player build a code on the entry row, the enter button submits it */
static void mm_entry(mm_state_t *state, uint8_t entry[MM_PEGS])
{
    uint8_t row;
    uint8_t col;

    memset(entry, 0, MM_PEGS);
    game_input_reset();

    while (1)
    {
        mm_render(state, entry);
        game_frame();

        if (!game_poll_press(&row, &col))
        {
            continue;
        }

        if (row != MM_ENTRY_ROW)
        {
            continue;
        }

        if (col < MM_PEGS)
        {
            entry[col] = (entry[col] + 1) % MM_COLORS;
        }
        else if (col == MM_ENTER_COL)
        {
            return;
        }
    }
}

void mastermind_run(void)
{
    mm_state_t state;
    uint8_t entry[MM_PEGS];
    uint8_t single_player;
    uint8_t exact = 0;
    uint8_t partial = 0;
    char result[24];
    int i;

    single_player = game_select_players();
    memset(&state, 0, sizeof(state));

    if (single_player == GAME_PLAYER_NONE)
    {
        /* two players: the second player secretly sets the code */
        mm_entry(&state, entry);
        memcpy(state.code, entry, MM_PEGS);
    }
    else
    {
        /* one player: the computer picks a random code */
        for (i = 0; i < MM_PEGS; i++)
        {
            state.code[i] = game_random() % MM_COLORS;
        }
    }

    /* build a first empty entry row for the guess phase */
    memset(entry, 0, MM_PEGS);
    game_input_reset();

    while (state.guess_count < MM_GUESSES)
    {
        uint8_t row;
        uint8_t col;

        mm_render(&state, entry);
        game_frame();

        if (!game_poll_press(&row, &col))
        {
            continue;
        }

        if (row != MM_ENTRY_ROW)
        {
            continue;
        }

        if (col < MM_PEGS)
        {
            entry[col] = (entry[col] + 1) % MM_COLORS;
        }
        else if (col == MM_ENTER_COL)
        {
            memcpy(state.guesses[state.guess_count], entry, MM_PEGS);
            mm_score(&state, entry, &exact, &partial);
            state.exact[state.guess_count] = exact;
            state.partial[state.guess_count] = partial;
            state.guess_count++;

            if (exact == MM_PEGS)
            {
                /* cracked it */
                if (single_player == GAME_PLAYER_NONE)
                {
                    game_show_result(GAME_PLAYER_RED); /* the guesser wins */
                }
                else
                {
                    game_show_text("CRACKED", mm_enter_color);
                }
                return;
            }

            memset(entry, 0, MM_PEGS); /* start the next guess */
        }
    }

    /* out of guesses: show the code in the result text */
    snprintf(result, sizeof(result), "CODE %u%u%u%u", (unsigned)(state.code[0] + 1), (unsigned)(state.code[1] + 1), (unsigned)(state.code[2] + 1), (unsigned)(state.code[3] + 1));
    if (single_player == GAME_PLAYER_NONE)
    {
        game_show_result(GAME_PLAYER_BLUE); /* the code setter wins */
    }
    else
    {
        game_show_text(result, mm_lose_color);
    }
}
