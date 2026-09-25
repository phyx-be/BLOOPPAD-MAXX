#include <ch32x035.h> /* both X033 and X035 */
#include <stdlib.h>   /* atoi() */

#include <wch_usbmidi_internal.h>

/* we use our own custom debug lib */
#include "debug.h"
#include "games.h"

#define VERSION_SHOW_MS (2000) /* how long the firmware version is shown at startup */
#define MENU_COL_MIDI   (GAME_COLS - 1)
#define MENU_NONE       (0xff) /* nothing selected in the menu yet */
#define MENU_BLINK_MS   (500)  /* blink period of the selected menu button */

typedef struct
{
    void (*run)(void); /* returns when the game is over */
    const char *name;  /* scrolls by when selected */
    game_color_t color;
} menu_item_t;

/* the startup menu on the first row, indexed by column */
static const menu_item_t menu[GAME_COLS] = {
    [0] = {.run = connect_four_run, .name = "CONNECT 4", .color = {.r = 255, .g = 255, .b = 0}},
    [1] = {.run = button_masher_run, .name = "MASHER", .color = {.r = 255, .g = 0, .b = 255}},
    [2] = {.run = tug_of_war_run, .name = "TUG OF WAR", .color = {.r = 0, .g = 255, .b = 255}},
    [3] = {.run = rainbow_run, .name = "RAINBOW", .color = {.r = 0, .g = 255, .b = 0}},
    [4] = {.run = life_run, .name = "GAME OF LIFE", .color = {.r = 255, .g = 100, .b = 0}},
    [5] = {.run = tetris_run, .name = "TETRIS", .color = {.r = 255, .g = 0, .b = 0}},
    [6] = {.run = battleships_run, .name = "BATTLESHIPS", .color = {.r = 0, .g = 0, .b = 255}},
    [MENU_COL_MIDI] = {.run = midi_run, .name = "MIDI", .color = {.r = 255, .g = 255, .b = 255}},
};

static const game_color_t menu_text_color = {.r = 255, .g = 255, .b = 255};

/* the states of the startup flow */
typedef enum
{
    STARTUP_VERSION, /* show the firmware version */
    STARTUP_CHOOSE,  /* nothing selected yet, "CHOOSE GAME" scrolls by */
    STARTUP_CONFIRM, /* the selected button blinks and its name scrolls by, press it again to start it */
    STARTUP_RUN,     /* run the selected game, then back to STARTUP_CONFIRM with that game selected */
} startup_state_t;

/* show a value in binary on a single row of leds, MSB in column 0 */
static void show_binary_row(uint8_t row, uint8_t value, game_color_t color)
{
    for (int col = 0; col < GAME_COLS; col++)
    {
        if (value & GAME_BUTTON(col))
        {
            game_set_led(row, col, color);
        }
    }
}

/* show the firmware version (built from git tags via VERSION_MAJOR/MINOR/PATCH macros) in binary:
 * major on row 0 (red), minor on row 1 (green), patch on row 2 (blue)
 */
static void show_version(void)
{
    board_clear_leds();
    show_binary_row(0, atoi(VERSION_MAJOR) & 0xff, (game_color_t){.r = 255, .g = 0, .b = 0});
    show_binary_row(1, atoi(VERSION_MINOR) & 0xff, (game_color_t){.r = 0, .g = 255, .b = 0});
    show_binary_row(2, atoi(VERSION_PATCH) & 0xff, (game_color_t){.r = 0, .g = 0, .b = 255});
    board_show_leds();
    Delay_Ms(VERSION_SHOW_MS);
}

/* draw the menu on the first row and the text below it, the selected item blinks */
static void render_menu(const char *text, uint8_t selected, uint32_t elapsed_ms)
{
    game_render_text(text, elapsed_ms, menu_text_color);
    for (int col = 0; col < GAME_COLS; col++)
    {
        uint8_t blink_off = (col == selected) && (elapsed_ms % MENU_BLINK_MS) >= (MENU_BLINK_MS / 2);
        if (menu[col].run && !blink_off)
        {
            game_set_led(0, col, menu[col].color);
        }
    }
}

/* the startup flow: show the version, let the user pick a game on the first row and confirm it */
static void startup(void)
{
    startup_state_t startup_state = STARTUP_VERSION;
    uint8_t selected = MENU_NONE;
    uint32_t start = 0;
    uint8_t row;
    uint8_t col;

    while (1)
    {
        switch (startup_state)
        {
            case STARTUP_VERSION:
                show_version();
                /* buttons that are held now only count after they are released */
                game_input_reset();
                start = game_millis();
                startup_state = STARTUP_CHOOSE;
                break;

            case STARTUP_CHOOSE:
            case STARTUP_CONFIRM:
                render_menu(startup_state == STARTUP_CHOOSE ? "CHOOSE GAME" : menu[selected].name, selected, game_millis() - start);
                game_frame();

                if (!game_poll_press(&row, &col) || row != 0 || !menu[col].run)
                {
                    break;
                }

                if (startup_state == STARTUP_CONFIRM && col == selected)
                {
                    startup_state = STARTUP_RUN;
                }
                else
                {
                    /* (re)select, restart the text and the blinking */
                    selected = col;
                    start = game_millis();
                    startup_state = STARTUP_CONFIRM;
                }
                break;

            case STARTUP_RUN:
                PRINT("starting %s\r\n", menu[selected].name);
                /* the moment of the press is the best source of randomness we have */
                game_seed_random(game_millis() * 2654435761u);
                menu[selected].run();

                /* the game is over: keep it selected, so one press plays it again;
                 * the button that ended the game only counts after a release
                 */
                game_input_reset();
                start = game_millis();
                startup_state = STARTUP_CONFIRM;
                break;
        }
    }
}

int main(void)
{
    SystemInit();
#ifdef NVIC_PriorityGroup_2
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
#else
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
#endif
    SystemCoreClockUpdate();
    Delay_Init();
    Delay_Ms(1000);

    PRINT("SystemClk: %u\r\n", (unsigned)SystemCoreClock);
    PRINT("ChipID: %08x\r\n", (unsigned)DBGMCU_GetCHIPID());

    /* initialize the LEDs and the button matrix */
    board_init();

    /* initialize USB MIDI */
    USB_init();

    PRINT("BloopPad Maxx Init done\r\n");

    startup();
}
