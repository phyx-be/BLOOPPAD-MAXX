#include <ch32x035.h> /* both X033 and X035 */
#include <string.h>   /* memset() */

#include <wch_usbmidi_internal.h>

/* we use our own custom debug lib */
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

/* midi-usb */
#define MIDI_CHANNEL (0)
#define MIDI_MAX     (0x7f)

/* USB-MIDI Code Index Numbers (CIN), USB MIDI spec table 4-1 */
#define MIDI_CIN_SYSEX_START_CONT  (0x04) /* SysEx starts or continues */
#define MIDI_CIN_SYSEX_END_1BYTE   (0x05) /* SysEx ends with following single byte, or 1-byte System Common */
#define MIDI_CIN_SYSEX_END_2BYTE   (0x06) /* SysEx ends with following two bytes, or empty SysEx */
#define MIDI_CIN_SYSEX_END_3BYTE   (0x07) /* SysEx ends with following three bytes */
#define MIDI_CIN_NOTE_OFF          (0x08)
#define MIDI_CIN_NOTE_ON           (0x09)
#define MIDI_CIN_POLY_KEY_PRESSURE (0x0A)
#define MIDI_CIN_CONTROL_CHANGE    (0x0B)
#define MIDI_CIN_PROGRAM_CHANGE    (0x0C)
#define MIDI_CIN_CHANNEL_PRESSURE  (0x0D)
#define MIDI_CIN_PITCH_BEND        (0x0E)
#define MIDI_CIN_SINGLE_BYTE       (0x0F) /* System Real-Time */
#define MIDI_CIN_MASK              (0x0F)

/* MIDI channel voice/system status bytes */
#define MIDI_STATUS_CONTROL_CHANGE (0xB0)
#define MIDI_STATUS_BIT            (0x80) /* set on every status byte, clear on every data byte */
#define MIDI_CHANNEL_MASK          (0x0F)
#define MIDI_SYSEX_START           (0xF0)
#define MIDI_SYSEX_END             (0xF7)
#define MIDI_TUNE_REQUEST          (0xF6)

/* SysEx manufacturer ID bytes identifying our custom BloopPad Maxx LED protocol */
#define SYSEX_MANUFACTURER_ID_1 (0x13)
#define SYSEX_MANUFACTURER_ID_2 (0x37)

typedef struct
{
    uint8_t g; /* Green */
    uint8_t r; /* Red */
    uint8_t b; /* Blue */
} ws2812b_color_t;

/* Predefined color palette used when the host sends a CC value 1–9 to set an LED.
 * Stored in GRB order (WS2812 wire format). Value 0 turns the LED off.
 */
static const ws2812b_color_t mixxx_palette[] = {
    {.g = 0x00, .r = 0x00, .b = 0x00}, /* 0: led off */
    {.g = 0x0a, .r = 0xc5, .b = 0x08}, /* 1: orange-red */
    {.g = 0xbe, .r = 0x32, .b = 0x44}, /* 2: teal */
    {.g = 0xd4, .r = 0x42, .b = 0xf4}, /* 3: yellow-green */
    {.g = 0xd2, .r = 0xf8, .b = 0x00}, /* 4: warm white */
    {.g = 0x44, .r = 0x00, .b = 0xff}, /* 5: blue */
    {.g = 0x00, .r = 0xaf, .b = 0xcc}, /* 6: cyan */
    {.g = 0xa6, .r = 0xfc, .b = 0xd7}, /* 7: white */
    {.g = 0xf2, .r = 0xf2, .b = 0xff}, /* 8: bright white */
    {.g = 0x80, .r = 0xff, .b = 0x00}, /* 9: green */
};

typedef struct
{
    uint8_t flag_update_leds : 1;      /* flag to indicate that the LEDs have to be updated with a new value from USB-MIDI */
    uint8_t flag_matrix_scan_done : 1; /* flag to indicate that the button matrix state has changed */
    uint8_t reserved : 6;              /* reserved for future use */
    uint8_t matrix_state[N_COLS];      /* button state bytes */
    ws2812b_color_t leds[LEDS_NUM];    /* LED data */
} addon_state_t;

/* Global Variables */
static addon_state_t state;

/* variables for MIDI SysEx processing */
#define MAX_SYSEX_DATA ((4 * LEDS_NUM) + 4)
static uint8_t in_sysex = 0;
static uint8_t sysex_data[MAX_SYSEX_DATA];
static int sysex_data_len = 0;

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
void SPI_1Lines_HalfDuplex_Init(void)
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
            state.flag_matrix_scan_done = 1; /* indicate that a full scan was finished */
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

/* execute a led animation */
static void led_boot_sequence()
{
    setColor(255, 0, 0);
    w2812_sync();
    Delay_Ms(500);
    setColor(0, 255, 0);
    w2812_sync();
    Delay_Ms(500);
    setColor(0, 0, 255);
    w2812_sync();
    Delay_Ms(500);
}

/* send a USB packet */
static void USBSendPacket(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t packet[4];
    packet[0] = (cin & MIDI_CIN_MASK); /* Cable 0 */
    packet[1] = b1;
    packet[2] = b2;
    packet[3] = b3;
    USB_write(packet, 4);
}

/* Send a MIDI Control Change message over USB.
 * Used to report button press/release: control = CC# encoding (row, col),
 * value = MIDI_MAX (0x7f) on press, 0 on release. */
static void USBSendControlChange(uint8_t channel, uint8_t control, uint8_t value)
{
    USBSendPacket(MIDI_CIN_CONTROL_CHANGE, MIDI_STATUS_CONTROL_CHANGE | (channel & MIDI_CHANNEL_MASK), control, value);
}

static void finalize_sysex(void)
{
    if (sysex_data_len < 8 || (sysex_data_len % 4) != 0)
    {
        PRINT("Unsupported SysEx size: %d\r\n", sysex_data_len);
        goto out;
    }

    if (sysex_data[1] != SYSEX_MANUFACTURER_ID_1 || sysex_data[2] != SYSEX_MANUFACTURER_ID_2)
    {
        PRINT("SysEx: Unsupported manufacturing ID: 0x%02X%02X\r\n", sysex_data[1], sysex_data[2]);
        goto out;
    }

    for (int i = 3; (i + 4) < sysex_data_len && sysex_data[i] != MIDI_SYSEX_END; i += 4)
    {

        uint8_t row = sysex_data[i] >> 4;
        uint8_t col = sysex_data[i] & 0x0F;

        if (row >= N_ROWS)
        {
            /* invalid row index */
            PRINT("SysEx: invalid row index sysex_data[%d] 0x%x\r\n", i, sysex_data[i]);
            continue;
        }

        if (col < 0x08)
        {
            /* col must be 0x08–0x0F for LED addressing (low nibble 0–7 is button-event space) */
            PRINT("SysEx: invalid column index sysex_data[%d] 0x%x\r\n", i, sysex_data[i]);
            continue;
        }

        uint8_t led_idx = ((col - 0x08) * N_ROWS) + row;

        /* SysEx data bytes are 7-bit (0-127);
         * shift left to scale into the 8-bit 0-254 color range
         */
        state.leds[led_idx].r = sysex_data[i + 1] << 1;
        state.leds[led_idx].g = sysex_data[i + 2] << 1;
        state.leds[led_idx].b = sysex_data[i + 3] << 1;
        state.flag_update_leds = 1;
    }

out:
    in_sysex = 0;
    sysex_data_len = 0;
}

static void handle_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t channel = b1 & MIDI_CHANNEL_MASK;
    uint8_t row;
    uint8_t col;
    uint8_t led_idx;

    switch (cin)
    {
        case MIDI_CIN_NOTE_OFF:
            PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case MIDI_CIN_NOTE_ON:
            if (b3 > 0)
            {
                PRINT("note on: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            else if (b3 == 0)
            {
                PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            break;

        case MIDI_CIN_POLY_KEY_PRESSURE:
            PRINT("Poly key pressure: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case MIDI_CIN_CONTROL_CHANGE:
            if (channel != MIDI_CHANNEL)
            {
                PRINT("we ignore channel %d\r\n", channel);
                break;
            }

            row = b2 >> 4;
            col = b2 & 0x0F;

            if (row >= N_ROWS)
            {
                /* invalid row index */
                PRINT("Control Change: invalid row index b2 0x%x\r\n", b2);
                break;
            }

            if (col < 0x08)
            {
                /* low nibble 0–7: this is a button-event CC#, not an LED command */
                PRINT("Control Change: invalid column index b2 0x%x\r\n", b2);
                break;
            }

            if (b3 < (sizeof(mixxx_palette) / sizeof(mixxx_palette[0])))
            {
                led_idx = ((col - 0x08) * N_ROWS) + row;
                state.leds[led_idx] = mixxx_palette[b3];
                state.flag_update_leds = 1;
            }
            else
            {
                PRINT("Control Change: invalid color index b3 0x%x\r\n", b3);
            }
            break;

        case MIDI_CIN_PROGRAM_CHANGE:
            PRINT("Program change: channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case MIDI_CIN_CHANNEL_PRESSURE:
            PRINT("Channel Pressure (Aftertouch): channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case MIDI_CIN_PITCH_BEND:
            /* Reconstruct 14-bit value from LSB (b2) and MSB (b3) */
            int val = (b2 & 0x7F) | ((b3 & 0x7F) << 7);
            val -= 8192; /* Center at 0 */
            PRINT("Pitch bend: channel %d, val %d\r\n", channel, val);
            break;

        case MIDI_CIN_SINGLE_BYTE:
            PRINT("single byte: 0x%02x\r\n", b1);
            break;

        case MIDI_CIN_SYSEX_START_CONT:

            /* SysEx start */
            if (b1 == MIDI_SYSEX_START)
            {
                sysex_data_len = 0;
                in_sysex = 1;
            }

            /* SysEx continues — but only if b1 is not a status byte */
            if (((in_sysex && !(b1 & MIDI_STATUS_BIT)) || b1 == MIDI_SYSEX_START) && sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
            }

            break;

        case MIDI_CIN_SYSEX_END_1BYTE: /* could be SysEx end (1-byte) OR standard 1-byte System Common (Tune Request) */
            if (in_sysex && b1 == MIDI_SYSEX_END && sysex_data_len < MAX_SYSEX_DATA)
            {
                sysex_data[sysex_data_len++] = b1;
                finalize_sysex();
            }
            else if (b1 == MIDI_TUNE_REQUEST)
            {
                // handle_tune_request();
                PRINT("Handle tune request\r\n");
            }
            break;

        case MIDI_CIN_SYSEX_END_2BYTE: /* SysEx end (2-byte) or empty SysEx */
            if (b1 == MIDI_SYSEX_START && !in_sysex)
            {
                // Rare: entire 2-byte SysEx (F0 F7) — Empty SysEx
                // process_sysex(...);
                PRINT("Empty SysEx\r\n");
                sysex_data_len = 0;
                in_sysex = 0;
            }
            else if (in_sysex && b2 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 1))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                finalize_sysex();
            }
            break;

        case MIDI_CIN_SYSEX_END_3BYTE:
            if (in_sysex && b3 == MIDI_SYSEX_END && sysex_data_len < (MAX_SYSEX_DATA - 2))
            {
                sysex_data[sysex_data_len++] = b1;
                sysex_data[sysex_data_len++] = b2;
                sysex_data[sysex_data_len++] = b3;
                finalize_sysex();
            }
            break;

        default:
            /* others ignored */
            break;
    }
}

/* main */
int main(void)
{
    uint8_t midi_pkt[4];
    uint8_t previous_kb_result[N_COLS] = {0};
    uint8_t current_kb_result[N_COLS] = {0};

    /* set all data and flags to 0 */
    memset(&state, 0, sizeof(addon_state_t));
    memset(midi_pkt, 0, 4);

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

    /* initialize TIM3 for button matrix scan */
    Matrix_Init(1, TIMER_FREQ); /* every 10 ms */

    /* configure SPI1 for WS2812 */
    SPI_1Lines_HalfDuplex_Init();
    SPI1_DMA_Init();
    DMA_Cmd(SPI1_DMA_TX_CH, ENABLE);

    /* initialize USB MIDI */
    USB_init();

    PRINT("BloopPad Maxx Init done\r\n");

    /* LED boot sequence */
    led_boot_sequence();

    /* clear all leds */
    setColor(0, 0, 0);
    w2812_sync();

    while (1)
    {
        if (state.flag_matrix_scan_done)
        {
            /* take a local copy of the current button state */
            state.flag_matrix_scan_done = 0;
            memcpy(current_kb_result, state.matrix_state, N_COLS);
        }

        if (USB_available())
        {
            if (USB_read(midi_pkt, 4) == 4)
            {
                handle_midi(midi_pkt[0] & MIDI_CIN_MASK, midi_pkt[1], midi_pkt[2], midi_pkt[3]);
            }
        }

        if (memcmp(previous_kb_result, current_kb_result, N_COLS) != 0)
        {
            for (int c = 0; c < N_COLS; c++)
            {
                for (int r = 0; r < N_ROWS; r++)
                {
                    uint8_t current_button_state = (current_kb_result[r] & (1 << (N_COLS - 1 - c))) & 0xff;
                    uint8_t previous_button_state = (previous_kb_result[r] & (1 << (N_COLS - 1 - c))) & 0xff;

                    if (current_button_state != previous_button_state)
                    {
                        USBSendControlChange(MIDI_CHANNEL, ((r << 4) & 0xF0) | c, current_button_state ? MIDI_MAX : 0);
                    }
                }
            }
            /* update the previous button state */
            memcpy(previous_kb_result, current_kb_result, N_COLS);
        }

        if (state.flag_update_leds)
        {
            state.flag_update_leds = 0;
            /* set the current led state to the leds */
            w2812_sync();
        }
    }
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
