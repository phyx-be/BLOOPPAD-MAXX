#include <ch32x035.h> /* both X033 and X035 */
#include <stdlib.h>   /* atoi() */
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

/* I2C on the expansion connector towards the badge */
#define SDA_PORT         GPIOC
#define SDA_PIN          GPIO_Pin_18
#define SCL_PORT         GPIOC
#define SCL_PIN          GPIO_Pin_19
#define I2C_ADDRESS      (0x55)
#define I2C_TIMEOUT      (-2)
#define I2C_TIMEOUT_TICK (1000)
#define I2C_SPEED        (400000)
#define UART_BAUDRATE    (115200)

/* midi-usb */
#define MIDI_CHANNEL (0)
#define MIDI_MAX     (0x7f)

/* 3 bytes: version number
 * N_COLS bytes: button matrix state
 * 3 * LEDS_NUM bytes: red, green, blue value for each LED
 * */
#define RESULT_BUFFER_SIZE (3 + N_COLS + (LEDS_NUM * 3))
#define RESULT_RW_OFFSET   (3 + N_COLS)

typedef struct
{
    uint8_t g; /* Green */
    uint8_t r; /* Red */
    uint8_t b; /* Blue */
} ws2812b_color_t;

/* Predefined color palette used when the host sends a CC value 1–9 to set an LED.
 * Stored in GRB order (WS2812 wire format). Index 0 = value 1, index 8 = value 9.
 * Value 0 turns the LED off (handled separately).
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

/*
 * This struct contains all data that is available through I2C.
 * Use the following command with a Buspirate to test:
 * read version number : [ 0xAA 0x00 [ 0xAB r:3 ]
 * read matrix state : [ 0xAA 0x03 [ 0xAB r:8 ]
 * turn on all leds : [ 0xAA 0x0B 0xFF:192 ]
 * turn off all leds : [ 0xAA 0x0B 0x00:192 ]
 * read everything : [ 0xAA 0x00 [ 0xAB r:203 ]
 */
typedef struct __attribute__((packed))
{
    uint8_t version[3];             /* version number */
    uint8_t matrix_state[N_COLS];   /* button state bytes in the result buffer */
    ws2812b_color_t leds[LEDS_NUM]; /* LED data in the result buffer */
} addon_data_t;

_Static_assert(sizeof(addon_data_t) == RESULT_BUFFER_SIZE, "raw data and struct size are not aligned!");

typedef struct
{
    uint8_t flag_update_leds : 1;       /* flag to indicate that the LEDs have to be updated with a new value from I2C/USB-MIDI */
    uint8_t flag_matrix_scan_done : 1;  /* flag to indicate that the button matrix state has changed */
    uint8_t flag_slave_first_write : 1; /* set on every ADDR phase; the next RXNE byte is the register offset. */
    uint8_t reserved : 5;               /* reserved for future use */
    uint8_t slave_offset;               /* register offset captured after the most recent ADDR+W. */
    uint8_t slave_position;             /* current read/write cursor, reset to offset on every ADDR (including repeated-START), so write-then-read works without special-casing. */
    union
    {
        addon_data_t data;
        uint8_t raw_data[RESULT_BUFFER_SIZE];
    };
} addon_state_t;

/* Global Variables */
static addon_state_t state;

/* buffer to hold the SPI data for WS2812 */
static uint8_t color_buf[COLOR_BUFFER_LEN] = {0};

static void USART3_Output_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4; /* PB3 (TX) PB4 (RX) */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);
}

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
        setPixelColor(i, state.data.leds[i].r, state.data.leds[i].g, state.data.leds[i].b);
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

/* function to process I2C slave data transfers */
/* reference: arduino implementation */
static void i2c_slave_process(void)
{
    uint32_t flag1 = 0, flag2 = 0;

    /* Snapshot all pending event flags in one read to avoid races. */
    flag1 = I2C1->STAR1;

    /* ADDR: our slave address was matched on the bus (start of any transaction).
     * Reset slave_position to slave_offset so that a repeated-START read begins
     * at the register the master last wrote, without needing a new WRITE phase.
     * Set flag_slave_first_write so the next RXNE byte is treated as the
     * register pointer rather than payload data.
     */
    if (flag1 & I2C_STAR1_ADDR)
    {
        state.slave_position = state.slave_offset;
        state.flag_slave_first_write = 1;
    }

    /* RXNE: receive data register not empty — master sent a byte.
     * The first byte after address+W is the register pointer; every byte
     * after that is payload to be written into the register map.
     */
    if (flag1 & I2C_STAR1_RXNE)
    {
        uint8_t byte = I2C_ReceiveData(I2C1);
        if (state.flag_slave_first_write)
        {
            /* Register pointer: latch it as both the persistent offset (used to
             * reset slave_position on repeated-START) and the current cursor.
             */
            state.slave_offset = byte;
            state.slave_position = byte;
            state.flag_slave_first_write = 0;
            PRINT("I2C reg: 0x%02x\r\n", byte);
        }
        else
        {
            if (state.slave_position < RESULT_BUFFER_SIZE)
            {
                if (state.slave_position >= RESULT_RW_OFFSET)
                {
                    /* Writable region (LED data): store the byte and notify the main loop. */
                    state.raw_data[state.slave_position] = byte;
                    state.flag_update_leds = 1;
                }
                else
                {
                    /* Read-only region: discard the byte silently.
                     * slave_position is still incremented below so the cursor advances
                     * even though we did not write, keeping alignment for any further bytes.
                     */
                    PRINT("ERROR: trying to write 0x%x to readonly data: 0x%x\r\n", byte, state.slave_position);
                }
            }
            state.slave_position++;
        }
    }

    /* Process transmitting data (master is reading from us).
     * Send one byte from raw_data[] at the current pointer position and advance
     * the pointer so consecutive TXE interrupts walk through the register file.
     * If slave_position is out of range, send 0x00 as a safe dummy byte.
     */
    if (flag1 & I2C_STAR1_TXE)
    {
        if (state.slave_position < RESULT_BUFFER_SIZE)
        {
            I2C_SendData(I2C1, state.raw_data[state.slave_position++]);
        }
        else
        {
            /* send dummy data */
            I2C_SendData(I2C1, 0x00);
        }
    }

    /* STOPF: master issued a STOP condition, ending the current transaction.
     * Hardware clears STOPF by: read STAR1 (done above) then write CTLR1.
     */
    if (flag1 & I2C_STAR1_STOPF)
    {
        PRINT("I2C STOP\r\n");
        /* writing CTLR1 after reading STAR1 clears STOPF */
        I2C1->CTLR1 &= ~(I2C_CTLR1_STOP);
    }

    /* Reading STAR2 releases clock stretching so the master can continue.
     * The dummy cast suppresses the unused-variable warning.
     */
    flag2 = I2C1->STAR2;
    (void)flag2;
}

/* initialize the I2C interface */
static void IIC_Init(uint32_t bound, uint16_t address)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStruct = {0};

    /* enable I2C1 and GPIOC clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* remap PC18/PC19 to I2C1 SDA/SCL */
    GPIO_PinRemapConfig(GPIO_PartialRemap3_I2C1, ENABLE); /* 011: Mapping (SCL/PC19, SDA/PC18) */

    /* Disable DIO (SWD) interface on these pins */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* configure the GPIO as SDA/SCL pins */
    GPIO_InitStructure.GPIO_Pin = SDA_PIN | SCL_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; /* automatic open-drain */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* configure I2C1 */
    I2C_InitStructure.I2C_ClockSpeed = bound;                                 /* bus speed */
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;                                /* there is only 1 mode */
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_16_9;                     /* I2C fast mode Tlow/Thigh = 16/9 */
    I2C_InitStructure.I2C_OwnAddress1 = address << 1;                         /* 7 or 10 bit address */
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;                               /* automatic acknowledge */
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; /* use 7 bit address */
    I2C_Init(I2C1, &I2C_InitStructure);

    /* configure I2C interrupts */
    NVIC_InitStruct.NVIC_IRQChannel = I2C1_EV_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    NVIC_InitStruct.NVIC_IRQChannel = I2C1_ER_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    /* Enable I2C event, error, and buffer interrupts.
     * EVT fires on: address match, byte received, byte transmitted, stop detected.
     * ERR fires on: bus error, arbitration lost, acknowledge failure, etc.
     * BUF fires on: TXE/RXNE (needed so we get an interrupt for each data byte).
     */
    I2C_ITConfig(I2C1, I2C_IT_EVT | I2C_IT_ERR | I2C_IT_BUF, ENABLE);

    /* Clock stretching: allow the slave to hold SCL low if it is not ready.
     * This prevents data loss when the interrupt handler is slightly slow.
     */
    I2C_StretchClockCmd(I2C1, ENABLE);

    /* enable I2C1 */
    I2C_Cmd(I2C1, ENABLE);
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
            memcpy(state.data.matrix_state, scan_result, N_COLS);
            state.flag_matrix_scan_done = 1; /* indicate that a full scan was finished */
            memset(scan_result, 0, N_COLS);
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
        state.data.leds[i].r = r;
        state.data.leds[i].g = g;
        state.data.leds[i].b = b;
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
    packet[0] = (cin & 0x0F); /* Cable 0 */
    packet[1] = b1;
    packet[2] = b2;
    packet[3] = b3;
    USB_write(packet, 4);
    /* In debug builds, UART3 is shared with the serial monitor, so MIDI output
     * over UART is disabled to avoid corrupting the debug stream. */
#ifndef DEBUG
    for (int i = 0; i < 4; i++)
    {
        while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
        {
            /* do nothing */
        }
        USART_SendData(USART3, packet[i]);
    }
#endif
}

/* Send a MIDI Control Change message over USB (and UART when not in debug mode).
 * Used to report button press/release: control = CC# encoding (row, col),
 * value = MIDI_MAX (0x7f) on press, 0 on release. */
static void USBSendControlChange(uint8_t channel, uint8_t control, uint8_t value)
{
    USBSendPacket(0x0B, 0xB0 | (channel & 0x0F), control, value);
}

static void handle_midi(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint8_t channel = b1 & 0x0F;
    uint8_t row;
    uint8_t col;
    uint8_t led_idx;

    switch (cin)
    {
        case 0x08: /* Note Off */
            PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case 0x09: /* Note On */
            if (b3 > 0)
            {
                PRINT("note on: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            else if (b3 == 0)
            {
                PRINT("note off: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            }
            break;

        case 0x0A: /* Poly Key Pressure */
            PRINT("Poly key pressure: channel %d, note %d, velocity %d\r\n", channel, b2, b3);
            break;

        case 0x0B: /* Control Change */
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
                state.data.leds[led_idx] = mixxx_palette[b3];
                state.flag_update_leds = 1;
            } else {
                PRINT("Control Change: invalid color index b3 0x%x\r\n", b3);
            }
            break;

        case 0x0C: /* Program Change */
            PRINT("Program change: channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case 0x0D: /* Channel Pressure (Aftertouch) */
            PRINT("Channel Pressure (Aftertouch): channel %d, b2 0x%x\r\n", channel, b2);
            break;

        case 0x0E: /* Pitch Bend */
            /* Reconstruct 14-bit value from LSB (b2) and MSB (b3) */
            int val = (b2 & 0x7F) | ((b3 & 0x7F) << 7);
            val -= 8192; /* Center at 0 */
            PRINT("Pitch bend: channel %d, val %d\r\n", channel, val);
            break;

        case 0x0F: /* Single Byte (Real Time) */
            PRINT("single byte: 0x%02x\r\n", b1);
            break;

        /* 0x05 could be SysEx end OR standard 1-byte System Common (Tune Request
         * 0xF6) */
        case 0x05:
            if (b1 >= 0xF8)
            { /* If it's real time embedded here (rare but legal) */
                PRINT("SysEx?: 0x%02x\r\n", b1);
            }
            break;

        default:
            /* SysEx (0x04, 0x06, 0x07) and others ignored */
            break;
    }
}

/* main */
int main(void)
{
    uint8_t midi_pkt[4];
    uint8_t uart_midi_pkt[4];
    uint8_t uart_midi_pkt_count = 0;
    uint8_t previous_kb_result[N_COLS] = {0};
    uint8_t current_kb_result[N_COLS] = {0};

    /* set all data and flags to 0 */
    memset(&state, 0, sizeof(addon_state_t));
    memset(midi_pkt, 0, 4);
    memset(uart_midi_pkt, 0, 4);

    /* set the version number from git */
    char version_major[] = VERSION_MAJOR;
    char version_minor[] = VERSION_MINOR;
    char version_patch[] = VERSION_PATCH;
    state.data.version[0] = atoi(version_major) & 0xff;
    state.data.version[1] = atoi(version_minor) & 0xff;
    state.data.version[2] = atoi(version_patch) & 0xff;

    SystemInit();
#ifdef NVIC_PriorityGroup_2
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
#else
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
#endif
    SystemCoreClockUpdate();
    Delay_Init();

    /* configure UART3 as serial monitor */
    USART3_Output_Init(UART_BAUDRATE);

    /* makes sure that we can still flash using SWD */
    Delay_Ms(1000); /* give serial monitor time to open */

    /* initialize i2c */
    IIC_Init(I2C_SPEED, I2C_ADDRESS); /* disables SWD */

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
            memcpy(current_kb_result, state.data.matrix_state, N_COLS);
        }

        if (USB_available())
        {
            if (USB_read(midi_pkt, 4) == 4)
            {
                handle_midi(midi_pkt[0] & 0x0F, midi_pkt[1], midi_pkt[2], midi_pkt[3]);
            }
        }

        /* receive MIDI packets from UART (non-blocking, one byte per iteration) */
        while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) != RESET)
        {
            uart_midi_pkt[uart_midi_pkt_count++] = USART_ReceiveData(USART3);
            if (uart_midi_pkt_count == 4)
            {
                uart_midi_pkt_count = 0;
                handle_midi(uart_midi_pkt[0] & 0x0F, uart_midi_pkt[1], uart_midi_pkt[2], uart_midi_pkt[3]);
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

/* Generic I2C1 IRQ (not used; event/error are handled by the dedicated handlers below) */
void I2C1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void I2C1_IRQHandler(void)
{
    PRINT("I2C1_IRQHandler\r\n");
}

/* I2C1 event interrupt: address match, data received/transmitted, stop detected */
void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void)
{
    i2c_slave_process();
}

/* I2C1 error interrupt: bus error, arbitration loss, acknowledge failure, etc. */
void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void)
{
    uint16_t STAR1 = I2C1->STAR1;
    if (STAR1 & I2C_STAR1_BERR) I2C1->STAR1 &= ~I2C_STAR1_BERR;
    if (STAR1 & I2C_STAR1_ARLO) I2C1->STAR1 &= ~I2C_STAR1_ARLO;
    if (STAR1 & I2C_STAR1_AF) I2C1->STAR1 &= ~I2C_STAR1_AF;
}
