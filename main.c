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

extern void Delay(volatile int i) {
    for (; i != 0; i--);
}

uint16_t row = 0;

const char pattern[] = {
//    0xaa, 0x14, 0xa5, 0x5a, 0x73, 0xc5, 0x93, 0x89
      0x14, 0x00, 0x51, 0x04, 0x00, 0x00, 0x00, 0x01,
      //0x00, 0x00, 0x00, 0x00, 0x40,
};

#define STATUSBAR_START 25
#define MAINWIN_START 120

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
    button_enter,
    button_exit
};

int button = button_none;

typedef void (*fn_t)(void);

int current_item = 0;

#define MENU_LENGTH ((menu_height / 16))

static void next(void) {
    current_item = (current_item + 1) % MENU_LENGTH;
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

static void prev(void) {
    current_item = (current_item + 1) % MENU_LENGTH;
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

const fn_t main_menu[][3] = {
    {NULL, prev, next},
    {NULL, prev, next},
    {NULL, prev, next},
    {NULL, prev, next},
    {NULL, prev, next},
    {NULL, prev, next}
};

void EXTI4_15_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line8) != RESET)
    {
        if (GPIOB->IDR & GPIO_Pin_7) {
            if (row < 300) {
                row++;
            }

            if (row < STATUSBAR_START) {
                if (button != button_none) {
                    main_menu[current_item][button]();
                    button = button_none;
                }
            } else if ((row > STATUSBAR_START) && (row < (STATUSBAR_START + 16))) {
                Delay(100);

                int i;
                const char * ptr = &statusbar_bits[row - STATUSBAR_START][0];
                SPI_SendData8(SPI1, ~(*(ptr++)));
                SPI_SendData8(SPI1, ~(*(ptr++)));
                for (i = 2; i < (statusbar_width / 8) - 2; i++) {
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                static bool batteryblink = true;
                static int count = 0;
                if (++count == 350) {
                    count = 0;
                    batteryblink = !batteryblink;
                    button = button_down;
                }
                if (batteryblink && battery_low) {
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                } else {
                    SPI_SendData8(SPI1, 0xff);
                    SPI_SendData8(SPI1, 0xff);
                }
                SPI_SendData8(SPI1, 0xff);

            } else if ((row > MAINWIN_START) && (row < (MAINWIN_START + menu_height - 1))) {
                Delay(160);

                int i;
                const char * ptr = &menu_bits[row - MAINWIN_START][0];
                bool selected = (row < start_inv) || (row > end_inv);
                if (selected) {
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                } else {
                    SPI_SendData8(SPI1, (*(ptr++)));
                    SPI_SendData8(SPI1, (*(ptr++)));
                }
                for (i = 2; i < (menu_width / 8); i++) {
                    uint8_t data = (*(ptr++));
                    if (selected) {
                        data ^= 0xff;
                    }
                    SPI_SendData8(SPI1, data);
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
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

int main(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;

/* Enable the GPIO_LED Clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
/* Configure the GPIO_LED pin */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    DAC_InitTypeDef    DAC_InitStructure;

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

    /* Enable GPIOA clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);

    /* Configure PA0 pin as input floating */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

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

    uint16_t ADCVal = 0x04ff;
    int16_t dir = +1;

    while(1)
    {
        /* Toggle LED */
        //GPIOB->ODR ^= GPIO_Pin_3;
        DAC_SetChannel1Data(DAC_Align_12b_R, ADCVal);
        if (dir > 0) {
            if (ADCVal == 0x4ff) {
                dir = -1;
            } else {
                //ADCVal++;
            }
        } else {
            if (ADCVal == 0x1ff) {
                dir = +1;
            } else {
                //ADCVal--;
            }
        }

        Delay(1000);

        /* Toggle LED */
        //GPIOB->ODR ^= GPIO_Pin_3;

        Delay(1000);
    }
}


