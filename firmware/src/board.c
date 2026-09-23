/* BloopPad Maxx hardware: the WS2812 LEDs and the button matrix */
#include <ch32x035.h> /* both X033 and X035 */
#include <string.h>   /* memset() */

#include "board.h"
#include "debug.h"

/* digital inputs: button matrix rows */
#define ROW0_PORT GPIOB /* PB12: Row 0 */
#define ROW0_PIN  GPIO_Pin_12
#define ROW1_PORT GPIOB /* PB11: Row 1 */
#define ROW1_PIN  GPIO_Pin_11
#define ROW2_PORT GPIOB /* PB10: Row 2 */
#define ROW2_PIN  GPIO_Pin_10
#define ROW3_PORT GPIOB /* PB9: Row 3 */
#define ROW3_PIN  GPIO_Pin_9
#define ROW4_PORT GPIOB /* PB8: Row 4 */
#define ROW4_PIN  GPIO_Pin_8
#define ROW5_PORT GPIOB /* PB7: Row 5 */
#define ROW5_PIN  GPIO_Pin_7
#define ROW6_PORT GPIOB /* PB6: Row 6 */
#define ROW6_PIN  GPIO_Pin_6
#define ROW7_PORT GPIOB /* PB5: Row 7 */
#define ROW7_PIN  GPIO_Pin_5
#define N_ROWS    (8)

/* digital outputs: button matrix columns */
#define COL0_PORT GPIOC /* PC0: Col 0 */
#define COL0_PIN  GPIO_Pin_0
#define COL1_PORT GPIOC /* PC3: Col 1 */
#define COL1_PIN  GPIO_Pin_3
#define COL2_PORT GPIOA /* PA0: Col 2 */
#define COL2_PIN  GPIO_Pin_0
#define COL3_PORT GPIOA /* PA1: Col 3 */
#define COL3_PIN  GPIO_Pin_1
#define COL4_PORT GPIOA /* PA2: Col 4 */
#define COL4_PIN  GPIO_Pin_2
#define COL5_PORT GPIOA /* PA3: Col 5 */
#define COL5_PIN  GPIO_Pin_3
#define COL6_PORT GPIOA /* PA4: Col 6 */
#define COL6_PIN  GPIO_Pin_4
#define COL7_PORT GPIOA /* PA5: Col 7 */
#define COL7_PIN  GPIO_Pin_5
#define N_COLS    (8)

#define TIMER_FREQ ((SystemCoreClock / 10000) - 1) /* the output frequency of all timers: 100Hz */

/* SPI1 for WS2812 LEDs */
#define LED_PORT         GPIOA /* PA7: WS2812 leds (SPI1 MOSI) */
#define LED_PIN          GPIO_Pin_7
#define LEDS_NUM         (N_ROWS * N_COLS)
#define Pixel_PRE_LEN    (12u)
#define Pixel_RESET_LEN  (25u)
#define COLOR_BUFFER_LEN (((LEDS_NUM * 3) * Pixel_PRE_LEN) + Pixel_RESET_LEN)
#define SPI1_DMA_TX_CH   DMA1_Channel3

typedef struct
{
    uint8_t g; /* Green */
    uint8_t r; /* Red */
    uint8_t b; /* Blue */
} ws2812b_color_t;

typedef struct
{
    uint8_t matrix_state[N_COLS];   /* button state bytes, written by the matrix scan interrupt */
    ws2812b_color_t leds[LEDS_NUM]; /* LED data */
} board_state_t;

static board_state_t state;

/* buffer to hold the SPI data for WS2812 */
static uint8_t color_buf[COLOR_BUFFER_LEN] = {0};

/* WS2812 LEDs use a 1-wire protocol where a '1' bit is a ~0.8µs high pulse and
 * a '0' bit is a ~0.4µs high pulse. We drive the data line via SPI at ~6 MHz
 * (one SPI bit ≈ 167 ns), which means each WS2812 bit maps to 4 SPI bits:
 *   WS2812 '1' → 1110 (0xE in a nibble)
 *   WS2812 '0' → 1000 (0x8 in a nibble)
 * Two WS2812 bits are packed into one SPI byte, so each LED color byte (8 bits)
 * expands to 4 SPI bytes.  Three color channels (GRB order) → 12 SPI bytes per LED
 * (Pixel_PRE_LEN = 12). The reset pulse is at least 50µs of low; Pixel_RESET_LEN
 * zero-bytes pad the end of the DMA buffer.
 */

/*********************************************************************
 * @fn      convToBit
 *
 * @brief   Encode one byte of WS2812 color data into 4 SPI bytes.
 *          Each input bit becomes a nibble: 0xE for a '1', 0x8 for a '0'.
 *          Two nibbles are packed per output byte, MSB first.
 *
 * @param   res   - output buffer (must have room for 4 bytes)
 *          input - color channel byte to encode
 *
 * @return  none
 */
static void convToBit(uint8_t *res, uint8_t input)
{
    uint8_t mask = 0x80;
    for (int i = 0; i < 4; i++)
    {
        uint8_t result = (input & mask) ? 0xE : 0x8;
        result <<= 4;
        mask >>= 1;
        result |= (input & mask) ? 0xE : 0x8;
        mask >>= 1;
        res[i] = result;
    }
}

/*********************************************************************
 * @fn      colorToBit
 *
 * @brief   Convert color to spi bit
 *
 * @param   buf  - the result
 *          r  - red channel
 *          g  - green channel
 *          b  - blue channel
 *
 * @return  none
 */
static void colorToBit(uint8_t *buf, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *res = buf;
    convToBit(res, g);
    convToBit(&(res[4]), r);
    convToBit(&(res[8]), b);
}

/*********************************************************************
 * @fn      setPixelColor
 *
 * @brief   Set the pixel color of an LED
 *
 * @param   index - index of LED
 *          r  - red channel
 *          g  - green channel
 *          b  - blue channel
 *
 *
 * @return  none
 */
static void setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *buf = &(color_buf[index * Pixel_PRE_LEN]);
    colorToBit(buf, r, g, b);
}

/*********************************************************************
 * @fn      SPI_1Lines_HalfDuplex_Init
 *
 * @brief   Configuring the SPI for half-duplex communication.
 *
 * @return  none
 */
static void SPI_1Lines_HalfDuplex_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef SPI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16; /* TODO: check if we are at 6M with this, or use SPI_BaudRatePrescaler_8? */
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}

/*********************************************************************
 * @fn      SPI1_DMA_Init
 *
 * @brief   Initialize DMA for SPI1
 *
 * @return  none
 */
static void SPI1_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(SPI1_DMA_TX_CH);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)color_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = COLOR_BUFFER_LEN;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(SPI1_DMA_TX_CH, &DMA_InitStructure);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
}

/*********************************************************************
 * @fn      w2812_sync
 *
 * @brief   Write data to LEDs
 *
 * @return  none
 */
static void w2812_sync(void)
{
    /* copy from internal buffer to SPI buffer */
    for (int i = 0; i < LEDS_NUM; i++)
    {
        setPixelColor(i, state.leds[i].r, state.leds[i].g, state.leds[i].b);
    }

    /* Wait for the previous DMA transfer to finish, then restart it.
     * DMA_Mode_Normal does not reload automatically, so we must disable,
     * reset the counter, and re-enable to send the next frame. */
    while (DMA_GetCurrDataCounter(SPI1_DMA_TX_CH) != 0)
    {
        /* do nothing */
    }
    DMA_ClearFlag(DMA1_FLAG_TC3);
    DMA_Cmd(SPI1_DMA_TX_CH, DISABLE);
    DMA_SetCurrDataCounter(SPI1_DMA_TX_CH, COLOR_BUFFER_LEN);
    DMA_Cmd(SPI1_DMA_TX_CH, ENABLE);
}

/* activate a single column of the button matrix (=set to low) */
static void Matrix_Set_Col(uint8_t col)
{
    switch (col)
    {
        case 0:
            GPIO_WriteBit(COL7_PORT, COL7_PIN, Bit_SET);
            GPIO_WriteBit(COL0_PORT, COL0_PIN, Bit_RESET);
            break;
        case 1:
            GPIO_WriteBit(COL0_PORT, COL0_PIN, Bit_SET);
            GPIO_WriteBit(COL1_PORT, COL1_PIN, Bit_RESET);
            break;
        case 2:
            GPIO_WriteBit(COL1_PORT, COL1_PIN, Bit_SET);
            GPIO_WriteBit(COL2_PORT, COL2_PIN, Bit_RESET);
            break;
        case 3:
            GPIO_WriteBit(COL2_PORT, COL2_PIN, Bit_SET);
            GPIO_WriteBit(COL3_PORT, COL3_PIN, Bit_RESET);
            break;
        case 4:
            GPIO_WriteBit(COL3_PORT, COL3_PIN, Bit_SET);
            GPIO_WriteBit(COL4_PORT, COL4_PIN, Bit_RESET);
            break;
        case 5:
            GPIO_WriteBit(COL4_PORT, COL4_PIN, Bit_SET);
            GPIO_WriteBit(COL5_PORT, COL5_PIN, Bit_RESET);
            break;
        case 6:
            GPIO_WriteBit(COL5_PORT, COL5_PIN, Bit_SET);
            GPIO_WriteBit(COL6_PORT, COL6_PIN, Bit_RESET);
            break;
        case 7:
            GPIO_WriteBit(COL6_PORT, COL6_PIN, Bit_SET);
            GPIO_WriteBit(COL7_PORT, COL7_PIN, Bit_RESET);
            break;
        default:
            GPIO_WriteBit(GPIOC, COL0_PIN | COL1_PIN, Bit_SET);
            GPIO_WriteBit(GPIOA, COL2_PIN | COL3_PIN | COL4_PIN | COL5_PIN | COL6_PIN | COL7_PIN, Bit_SET);
    }
}

/*********************************************************************
 * @fn      Matrix_Scan
 *
 * @brief   Perform button matrix scan.
 *
 * @return  none
 */
/* Matrix_Scan is called from TIM3.  It scans one column at a time using a
 * 2-sample debounce:
 *   - at tick 5:  take the first sample of the active column's rows
 *   - at tick 10: take a second sample; only commit if both agree
 * scan_col advances after each 10-tick cycle; a complete 8-column scan
 * finishes every 80 TIM3 ticks.
 *
 * Hardware: row inputs use pull-ups (GPIO_Mode_IPU), columns are driven low
 * to select them (active-low).  A pressed button pulls the row pin low → bit=0.
 * The raw value is inverted (~scan) so that a pressed button maps to bit=1.
 * Row pins PB5–PB12 are shifted right by 5 to land in bits [7:0].
 */
static void Matrix_Scan(void)
{
    static uint8_t scan_cnt = 0;
    static uint8_t scan_col = 0;
    static uint8_t scan_result[N_COLS] = {0x00};
    static uint8_t scan = 0;

    scan_cnt++;
    if ((scan_cnt % 10) == 0)
    {
        scan_cnt = 0;

        /* second sample: accept only if it matches the first sample (debounce) */
        if (scan == ((GPIO_ReadInputData(GPIOB) >> 5) & 0xff))
        {
            /* both samples agree — store the result for this column (active-low → invert) */
            scan_result[scan_col] = ~scan;
        }
        else
        {
            /* samples disagree (bouncing) — keep the previous state for this column */
        }

        /* activate the next column */
        scan_col = (scan_col + 1) % N_COLS;
        Matrix_Set_Col(scan_col);

        /* all columns were scanned
         * write the result and notify if it is changed
         */
        if (scan_col == 0)
        {
            memcpy(state.matrix_state, scan_result, N_COLS);
        }
    }
    else if ((scan_cnt % 5) == 0)
    {
        /* first sample: record the raw row state mid-period */
        scan = (GPIO_ReadInputData(GPIOB) >> 5) & 0xff;
    }
}

/*********************************************************************
 * @fn      Matrix_Init
 *
 * @brief   Initialize matrix gpio and timer3 for button matrix scan
 *
 * @param   arr - The specific period value
 *          psc - The specifies prescaler value
 *
 * @return  none
 */
static void Matrix_Init(uint16_t arr, uint16_t psc)
{

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* enable GPIOA, GPIOB, GPIOC, and AFIO clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    /* Enable Timer3 Clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* the columns are the outputs */
    GPIO_InitStructure.GPIO_Pin = COL0_PIN | COL1_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = COL2_PIN | COL3_PIN | COL4_PIN | COL5_PIN | COL6_PIN | COL7_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* the rows are the inputs */
    GPIO_InitStructure.GPIO_Pin = ROW0_PIN | ROW1_PIN | ROW2_PIN | ROW3_PIN | ROW4_PIN | ROW5_PIN | ROW6_PIN | ROW7_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Initialize Timer3 */
    TIM_TimeBaseStructure.TIM_Period = arr;
    TIM_TimeBaseStructure.TIM_Prescaler = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    /* enable timer interrupts */
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    /* configure timer interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* deactivate all columns */
    Matrix_Set_Col(99);

    /* activate the first column */
    Matrix_Set_Col(0);

    /* Enable Timer3 */
    TIM_Cmd(TIM3, ENABLE);
}

/* set all leds to the same color */
static void setColor(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LEDS_NUM; i++)
    {
        state.leds[i].r = r;
        state.leds[i].g = g;
        state.leds[i].b = b;
    }
}

void board_init(void)
{
    memset(&state, 0, sizeof(board_state_t));

    /* initialize TIM3 for button matrix scan */
    Matrix_Init(1, TIMER_FREQ); /* every 10 ms */

    /* configure SPI1 for WS2812 */
    SPI_1Lines_HalfDuplex_Init();
    SPI1_DMA_Init();
    DMA_Cmd(SPI1_DMA_TX_CH, ENABLE);
}

void board_set_led(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t led_idx = (col * N_ROWS) + row;
    state.leds[led_idx].r = r;
    state.leds[led_idx].g = g;
    state.leds[led_idx].b = b;
}

void board_clear_leds(void)
{
    setColor(0, 0, 0);
}

void board_show_leds(void)
{
    w2812_sync();
}

void board_read_buttons(uint8_t rows[BOARD_ROWS])
{
    memcpy(rows, state.matrix_state, BOARD_ROWS);
}

/* interrupt handlers */
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        Matrix_Scan();
    }
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void NMI_Handler(void)
{
    PRINT("NMI_Handler\r\n");
}

void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void)
{
    PRINT("HARDFAULT\r\n");
    while (1)
    {
    }
}
