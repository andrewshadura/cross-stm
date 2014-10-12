#include "stm32f0xx_adc.h"
#include "stm32f0xx_dac.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_misc.h"
#include "stm32f0xx_syscfg.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_spi.h"

#include <stdbool.h>
#include <stdlib.h>

#include "galaxians.h"
#include "statusbar.h"
#include "menu.h"
#include "cross-big.h"
#include "cross-tdot.h"
#include "cross-tdot-mini.h"
#include "cross-milt.h"

struct coords_t {
    uint16_t x;
    uint16_t y;
};

struct setting_t {
    struct coords_t coords[3];
    uint8_t brightness;
    uint8_t cross_type;
};

struct settings_t {
    /*
        for each user:
            for each zoom: coords
            brightness
            cross type
     */
    struct setting_t users[4];
    uint32_t seqno;
    uint32_t invalid;
};

struct settings_t __attribute__((section (".flash.page30"))) settings0 = {
    .invalid = 0xffffffff
};
struct settings_t __attribute__((section (".flash.page31"))) settings1 = {
    .invalid = 0xffffffff
};

struct settings_t settings;

extern void Delay(volatile int i) {
    for (; i != 0; i--);
}

uint16_t row = 0;

#define CHARGEN_PROGRESS 8

unsigned char statusbar_ram_bits[statusbar_width / 8];
unsigned char gauge_ram_bits[12];

#define STATUSBAR_START 25
#define MAINWIN_START 120
#define CROSS_CENTRE cross_y

#define CROSS_Y_DEFAULT 162
#define CROSS_X_DEFAULT 205

#define CROSS_X_RANGE 85
#define CROSS_Y_RANGE 85

uint16_t cross_y = CROSS_Y_DEFAULT; /* +/- 85 */
uint16_t cross_x = CROSS_X_DEFAULT;

uint16_t start_inv = MAINWIN_START + 3, end_inv = MAINWIN_START + 14;

bool battery_low = true;

#if 0
static void set_out(char i) {
    if (i) {
        GPIOA->ODR &= ~GPIO_Pin_7;
    } else {
        GPIOA->ODR |= GPIO_Pin_7;
    }
}
#endif

enum buttons {
    button_none = 0,
    button_up,
    button_down,
    button_menu,
    button_left,
    button_right,
    button_long,

    button_count,
    button_max = button_count - 1
};

enum cross_type {
    cross_type_min = 0,
    cross_type_big = cross_type_min,
    cross_type_tdot,
    cross_type_tdot_mini,
    cross_type_milt,

    cross_type_count,
    cross_type_max = cross_type_count - 1
};

const unsigned char cross_height[] = {
    [cross_type_big] = cross_big_height,
    [cross_type_tdot] = cross_tdot_height,
    [cross_type_tdot_mini] = cross_tdot_mini_height,
    [cross_type_milt] = cross_milt_height
};

int button = button_none;

int cross_type = cross_type_big;

bool show_cross = true;
bool show_gauge = false;

bool autorepeat = false;

int gauge_value = 0;

int menu = 0;

bool inverted = false;

#define HCOUNT_OFF   -2
#define HCOUNT_ON     0
#define HCOUNT_REPEAT 25
#define HCOUNT_LONG   35

typedef void (*fn_t)(int button);

int current_item = 0;

#define MENU_LENGTH ((menu_height / 16))

int current_input = 1;

int current_zoom = 2;

#define MAX_BRIGHTNESS 23

uint8_t brightness = MAX_BRIGHTNESS;

static void load_settings(void);
static void init_settings(void);
static void save_settings(void);

const char input_map[4] = {
    [0] /* 00 */ = 0,
    [1] /* 01 */ = 1,
    [3] /* 11 */ = 2,
    [2] /* 10 */ = 3
};

static void update_status(void) {
    int i;
    uint8_t input = ((GPIOA->IDR & (GPIO_Pin_2 | GPIO_Pin_1)) >> 1);
    current_input = input_map[input];

    statusbar_ram_bits[0] = current_input;
    for (i = 1; i <= 17; i++) {
        statusbar_ram_bits[i] = 0;
    }
    switch (current_zoom) {
        case 1:
            statusbar_ram_bits[14] = 4;
            statusbar_ram_bits[15] = 5;
            break;
        case 2:
            statusbar_ram_bits[14] = 6;
            statusbar_ram_bits[15] = 7;
            break;
        default:
            statusbar_ram_bits[14] = 0;
            statusbar_ram_bits[15] = 0;
            break;
    }

    for (i = 18; i < (statusbar_width / 8); i++) {
        statusbar_ram_bits[i] = i;
    }
}

static void update_gauge(void) {
    int last_pos = (gauge_value + 1) / 2;
    int last_part = (gauge_value + 1) % 2;
    int i;

    for (i = 0; i < sizeof(gauge_ram_bits); i++) {
        if (i < last_pos) {
            gauge_ram_bits[i] = CHARGEN_PROGRESS + 2;
        } else if (i == last_pos) {
            gauge_ram_bits[i] = CHARGEN_PROGRESS + last_part;
        } else {
            gauge_ram_bits[i] = CHARGEN_PROGRESS;
        }
    }
}

typedef const fn_t menu_t[][button_count];

static void next(int button);
static void prev(int button);
static void next_cross(int button);
static void prev_cross(int button);
static void switch_cross(int button);
static void switch_inversion(int button);
static void switch_menu(int button);
static void switch_zoom(int button);
static void set_brightness(int button);
static void change_brightness(int button);
static void finish_brightness(int button);
static void update_brightness(int value);
static void set_move_cross(int button);
static void cross_xy(int button);
static void finish_move(int button);

menu_t main_menu = {
    {NULL, prev, next, switch_cross,   NULL, NULL, switch_menu},
    {NULL, prev, next, set_brightness, NULL, NULL, switch_menu},
    {NULL, prev, next, NULL,           NULL, NULL, switch_menu},
    {NULL, prev, next, NULL,           NULL, NULL, switch_menu},
    {NULL, prev, next, set_move_cross, NULL, NULL, switch_menu},
    {NULL, prev, next, NULL,           NULL, NULL, switch_menu}
};

const unsigned char menu_widths[] = {
    8,
    12,
    13,
    15,
    10,
    9
};

menu_t off_menu = {
    {NULL, switch_zoom, switch_inversion, NULL, NULL, NULL, switch_menu}
};

menu_t switch_cross_menu = {
    {NULL, prev_cross, next_cross, switch_cross, prev_cross, next_cross, switch_menu}
};

menu_t brightness_menu = {
    {NULL, change_brightness, change_brightness, finish_brightness, change_brightness, change_brightness, switch_menu}
};

menu_t move_cross_menu = {
    {NULL, cross_xy, cross_xy, finish_move, cross_xy, cross_xy, switch_menu}
};

menu_t * current_menu = &main_menu;

static void next(int button) {
    current_item = (current_item + 1) % MENU_LENGTH;
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

static void prev(int button) {
    if (current_item == 0) {
        current_item = MENU_LENGTH - 1;
    } else {
        current_item--;
    }
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

static void switch_menu(int button) {
    if (menu) {
        menu = 0;
        show_cross = 1;
        show_gauge = 0;
    } else {
        menu = 1;
        show_cross = 0;
        show_gauge = 0;
    }
}

static void switch_inversion(int button) {
    inverted = !inverted;
    update_brightness(brightness);
}

static void next_cross(int button) {
    if (cross_type == cross_type_max) {
        cross_type = cross_type_min;
    } else {
        cross_type++;
    }
}

static void prev_cross(int button) {
    if (cross_type == cross_type_min) {
        cross_type = cross_type_max;
    } else {
        cross_type--;
    }
}

static void switch_cross(int button) {
    current_menu = &switch_cross_menu;
    switch (menu) {
        case 1:
            menu++;
            show_cross = true;
            break;
        case 2:
            menu--;
            show_cross = false;
            break;
    }
}

static void switch_zoom(int button) {
    if (current_zoom == 2) {
        current_zoom = 0;
    } else {
        current_zoom++;
    }
}

static void set_brightness(int button) {
    menu++;
    show_gauge = true;
    gauge_value = brightness;
    current_menu = &brightness_menu;
    update_brightness(brightness);
}

static void update_brightness(int value) {
    if (inverted) {
        DAC_SetChannel1Data(DAC_Align_12b_R, 0x3ff - 128 - value * 16);
    } else {
        DAC_SetChannel1Data(DAC_Align_12b_R, 0x3ff + value * 16);
    }
}

static void change_brightness(int button) {
    if ((button == button_left) || (button == button_down)) {
        gauge_value = (gauge_value == 0) ? gauge_value : gauge_value - 1;
    }
    if ((button == button_right) || (button == button_up)) {
        gauge_value = (gauge_value == 23) ? gauge_value : gauge_value + 1;
    }
    update_brightness(gauge_value);
}

static void finish_brightness(int button) {
    menu--;
    show_gauge = false;
    brightness = gauge_value;
}

static void set_move_cross(int button) {
    menu++;
    current_menu = &move_cross_menu;
    show_cross = true;
}

static void cross_xy(int button) {
    switch (button) {
        case button_left:
            if (cross_x > (CROSS_X_DEFAULT - CROSS_X_RANGE)) {
                cross_x--;
            }
            break;
        case button_right:
            if (cross_x < (CROSS_X_DEFAULT + CROSS_X_RANGE)) {
                cross_x++;
            }
            break;
        case button_up:
            if (cross_y > (CROSS_Y_DEFAULT - CROSS_Y_RANGE)) {
                cross_y--;
            }
            break;
        case button_down:
            if (cross_y < (CROSS_Y_DEFAULT + CROSS_Y_RANGE)) {
                cross_y++;
            }
            break;
    }
}

static void finish_move(int button) {
    menu--;
}

#define STEPS 5
#define STEPWIDTH (1024/STEPS)

uint8_t codes[] = {button_menu, button_up, button_down, button_right, button_left, 0};

volatile uint16_t adcval;
volatile uint16_t DACVal = 0x057f;

uint8_t debounced_code = 0;

volatile int8_t hcount = 0;

static void buttons(void)
{
    adcval = ADC_GetConversionValue(ADC1);
    ADC_StartOfConversion(ADC1);

    uint16_t temp = adcval / 4;
    temp += (STEPWIDTH/2);
    temp /= STEPWIDTH;
    debounced_code = codes[temp];

    if (debounced_code == 0) {
        hcount = HCOUNT_OFF;
    } else {
        if (hcount == HCOUNT_ON) {
            button = debounced_code;
        }
        if (hcount >= HCOUNT_REPEAT) {
            if ((debounced_code != button_menu)) {
                hcount = HCOUNT_ON;
                if (autorepeat) {
                    button = debounced_code;
                }
            } else {
                if (hcount == (HCOUNT_LONG - 1)) {
                    button = button_long;
                }
            }
        }
        if (hcount < HCOUNT_LONG) {
            hcount++;
        }
    }
}

void EXTI4_15_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line8) != RESET)
    {
        if (GPIOB->IDR & GPIO_Pin_7) {
            if (row < 400) {
                row++;
            }

            if (row == (STATUSBAR_START - 2)) {
                if (button != button_none) {
                    /*
                    if ((*current_menu)[current_item][button]) {
                        (*current_menu)[current_item][button]();
                    }
                    */
                    fn_t callback;
                    switch (menu) {
                        case 0:
                            callback = off_menu[0][button];
                            break;
                        case 1:
                            callback = main_menu[current_item][button];
                            break;
                        case 2:
                            callback = (*current_menu)[0][button];
                            break;
                        default:
                            callback = NULL;
                    }
                    if (callback) {
                        callback(button);
                    }
                    if (menu == 1) {
                        update_brightness(MAX_BRIGHTNESS);
                    }
                    button = button_none;
                }

                autorepeat = menu == 2;
                buttons();
                update_status();
                if (show_gauge) {
                    update_gauge();
                }
            } else if ((row > STATUSBAR_START) && (row < (STATUSBAR_START + 16))) {
                Delay(100);

                int i;
                int status_row = row - STATUSBAR_START;

                const char * ptr = &statusbar_bits[status_row][0];

                int current_width = menu_widths[current_item];

                if (menu < 2) {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[0]]));
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[1]]));
                    for (i = 2; i < (statusbar_width / 8) - 2; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                } else {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[0]]));
                    int left = 15 - (current_width + 1) / 2;
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[1]]));
                    int right = left + current_width;
                    for (i = 2; i < left; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                    const char * ptr2 = &menu_bits[start_inv - MAINWIN_START + status_row - 2][0];
                    for (; i < right; i++) {
                        SPI_SendData8(SPI1, ~(*(ptr2++)));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                    for (; i < (statusbar_width / 8) - 2; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
}
                static bool batteryblink = true;
                static int count = 0;
                if (++count == 350) {
                    count = 0;
                    batteryblink = !batteryblink;
                    //button = button_down;
                }
                if (batteryblink && battery_low) {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i + 1]]));
                } else {
                    SPI_SendData8(SPI1, 0xff);
                    SPI_SendData8(SPI1, 0xff);
                }
                SPI_SendData8(SPI1, 0xff);
            } else if ((menu == 1) && ((row > MAINWIN_START) && (row < (MAINWIN_START + menu_height - 1)))) {
                Delay(160);

                int i;
                const char * ptr;
                ptr = &menu_bits[row - MAINWIN_START][0];
                bool notselected = ((row < start_inv) | (row > end_inv));
                if (notselected) {
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                } else {
                    SPI_SendData8(SPI1, (*(ptr++)));
                    SPI_SendData8(SPI1, (*(ptr++)));
                }
                for (i = 2; i < (menu_width / 8); i++) {
                    uint8_t data = (*(ptr++));
                    if (notselected) {
                        data ^= 0xff;
                    }
                    SPI_SendData8(SPI1, data);
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);
            } else if ((show_cross) && ((row > (CROSS_CENTRE - cross_height[cross_type] / 2)) && (row < (CROSS_CENTRE + cross_height[cross_type] / 2 - 1)))) {
                uint16_t CROSS_START = CROSS_CENTRE - cross_height[cross_type] / 2;
                Delay(cross_x);

                int i;
                const char * ptr;
                const char * ptrs[] = {
                    [cross_type_big]  = &cross_big_lines[cross_big_bits[row - CROSS_START]][0],
                    [cross_type_tdot] = &cross_tdot_lines[cross_tdot_bits[row - CROSS_START]][0],
                    [cross_type_tdot_mini] = &cross_tdot_mini_lines[cross_tdot_mini_bits[row - CROSS_START]][0],
                    [cross_type_milt] = &cross_milt_lines[cross_milt_bits[row - CROSS_START]][0]
                };

                ptr = ptrs[cross_type];

                SPI_SendData8(SPI1, ~(*(ptr++)));
                SPI_SendData8(SPI1, ~(*(ptr++)));
                for (i = 2; i < (menu_width / 8); i++) {
                    uint8_t data = (*(ptr++));
                    data ^= 0xff;
                    SPI_SendData8(SPI1, data);
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);

            } else if ((show_gauge) && ((row > MAINWIN_START) && (row < (MAINWIN_START + 15)))) {
                Delay(160);

                int i;
                const char * ptr = &statusbar_bits[row - MAINWIN_START][0];

                //SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[0]]));
                //SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[1]]));
                for (i = 0; i < sizeof(gauge_ram_bits); i++) {
                    SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[i]]));
                    if (i > 1) while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);
            }
        } else {
            row = 0;
        }

        /* Clear the EXTI line 8 pending bit */
        EXTI_ClearITPendingBit(EXTI_Line8);
    }
}

static void init_settings(void) {
    int i;
    for (i = 0; i < 4; i++) {
        int j;
        for (j = 0; j < 3; j++) {
            settings.users[i].coords[j].x = CROSS_X_DEFAULT;
            settings.users[i].coords[j].y = CROSS_Y_DEFAULT;
        }
        settings.users[i].brightness = MAX_BRIGHTNESS;
        settings.users[i].cross_type = cross_type_min;
    }

    settings.seqno = 0;
    settings.invalid = 0;
}

static void load_settings(void) {
    if (settings0.invalid == 0) {
        memcpy(&settings, &settings0, sizeof(struct settings_t));
    } else {
        init_settings();
    }
}

static void save_settings(void) {
    //
}

int main(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    DAC_InitTypeDef DAC_InitStructure;

    /* Enable the GPIO_LED Clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    /* Configure the GPIO_LED pin */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Enable GPIOA clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Configure PA.04 (DAC1_OUT1) in analog mode -------------------------*/
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Enable DAC clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

    /* DAC1 channel1 Configuration */
    DAC_InitStructure.DAC_Trigger = DAC_Trigger_None;
    DAC_InitStructure.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &DAC_InitStructure);

    /* Enable DAC1 Channel1 */
    DAC_Cmd(DAC_Channel_1, ENABLE);

    /* Enable GPIOB clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);

    /* Configure PB8 pin as input floating */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    /* Connect EXTI7 Line to PB8 pin */
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOB, EXTI_PinSource8);

    /* Configure EXTI7 line */
    EXTI_InitStructure.EXTI_Line = EXTI_Line8;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* Enable and set EXTI4-15 Interrupt to the lowest priority */
    NVIC_InitStructure.NVIC_IRQChannel = EXTI4_15_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx; // Initially Tx
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High; // Clock steady high
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge; // Data write on rising (second) edge
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_LSB;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_RxFIFOThresholdConfig(SPI1, SPI_RxFIFOThreshold_QF);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI1, ENABLE);

    /* Turn on LED*/
    GPIOB->BSRR = GPIO_Pin_3;
    GPIOA->BSRR = GPIO_Pin_7;

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_0);

    /* ADC1 Periph clock enable */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* Configure ADC Channel11 as analog input */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_StructInit(&ADC_InitStructure);
    /* Configure the ADC1 in continuous mode withe a resolution equal to 12 bits*/
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_ScanDirection = ADC_ScanDirection_Upward;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* Convert the ADC1 Channel 0 with 1.5 Cycles as sampling time */
    ADC_ChannelConfig(ADC1, ADC_Channel_0, ADC_SampleTime_1_5Cycles);

    ADC_GetCalibrationFactor(ADC1);
    ADC_Cmd(ADC1, ENABLE);
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_ADRDY));
    ADC_WaitModeCmd(ADC1, ENABLE);
    ADC_StartOfConversion(ADC1);

    update_status();

    update_brightness(brightness);

    while(1)
    {
    }
}


