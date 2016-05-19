#include "stm32f0xx_gpio.h"
#include "stm32f0xx_i2c.h"
#include "stm32f0xx_rcc.h"

#include <stdint.h>
#include <stdbool.h>

#include "compass.h"

bool read_compass_request = false;
uint8_t compass_lock = 0;

void Compass_Setup(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    RCC_I2CCLKConfig(RCC_I2C1CLK_HSI);

    //(#) Enable peripheral clock using RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2Cx, ENABLE)
    //    function for I2C1 or I2C2.
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    //(#) Enable SDA, SCL  and SMBA (when used) GPIO clocks using
    //    RCC_AHBPeriphClockCmd() function.
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);

    //(#) Peripherals alternate function:
    //    (++) Connect the pin to the desired peripherals' Alternate
    //         Function (AF) using GPIO_PinAFConfig() function.
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_1);

    //    (++) Configure the desired pin in alternate function by:
    //         GPIO_InitStruct->GPIO_Mode = GPIO_Mode_AF
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;

    //    (++) Select the type, OpenDrain and speed via
    //         GPIO_PuPd, GPIO_OType and GPIO_Speed members
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    //    (++) Call GPIO_Init() function.
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    //(#) Program the Mode, Timing , Own address, Ack and Acknowledged Address
    //    using the I2C_Init() function.
    I2C_StructInit(&I2C_InitStructure);
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_AnalogFilter = I2C_AnalogFilter_Enable;
    I2C_InitStructure.I2C_DigitalFilter = 0;
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_OwnAddress1 = 0;
    I2C_InitStructure.I2C_Timing = 0x40B22536;
    I2C_Init(I2C1, &I2C_InitStructure);

    //(#) Enable the I2C using the I2C_Cmd() function.
    I2C_Cmd(I2C1, ENABLE);

}

static int state = 0;

void Compass_Reset(void) {
    state = 0;
}

bool Compass_Read(uint8_t reg, uint16_t *out) {
    static uint16_t tmp = 0;

    switch (state) {

        case 0:
            I2C_TransferHandling(I2C1, COMPASS_ADDR, 1, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);
            state++;
            break;

        case 1:
            if (I2C1->ISR & I2C_ISR_TXIS) {
                I2C_SendData(I2C1, reg);
                state++;
            }
            break;

        case 2:
            if (I2C1->ISR & I2C_ISR_TC) {
                I2C_TransferHandling(I2C1, COMPASS_ADDR, 2, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);
                state++;
            }
            break;

        case 3:
            if (I2C1->ISR & I2C_ISR_RXNE) {
                tmp = I2C_ReceiveData(I2C1);
                state++;
            }
            break;

        case 4:
            if (I2C1->ISR & I2C_ISR_RXNE) {
                tmp |= (I2C_ReceiveData(I2C1) << 8);
                state++;
            }
            break;

        case 5:
            if (I2C1->ISR & I2C_ISR_STOPF) {
                I2C1->ICR = I2C_ICR_STOPCF;
                state = 0;
                *out = tmp;
                return true;
            }
            break;
    }
    return false;
}

bool Compass_Write(uint8_t reg, uint8_t value) {
    switch (state) {

        case 0:
            I2C_TransferHandling(I2C1, COMPASS_ADDR, 2, I2C_AutoEnd_Mode, I2C_Generate_Start_Write);
            state++;
            break;

        case 1:
            if (I2C1->ISR & I2C_ISR_TXIS) {
                I2C_SendData(I2C1, reg);
                state++;
            }
            break;

        case 2:
            if (I2C1->ISR & I2C_ISR_TXIS) {
                I2C_SendData(I2C1, value);
                state++;
            }
            break;

        case 3:
            if (I2C1->ISR & I2C_ISR_STOPF) {
                I2C1->ICR = I2C_ICR_STOPCF;
                state = 0;
                return true;
            }
            break;
    }
    return false;
}


