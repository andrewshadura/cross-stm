#include "stm32f0xx_flash.h"
#include "flash_async.h"

void FLASH_ErasePage_AsyncStart(uint32_t Page_Address)
{
  /* Check the parameters */
  assert_param(IS_FLASH_PROGRAM_ADDRESS(Page_Address));

  {
    /* If the previous operation is completed, proceed to erase the page */
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR  = Page_Address;
    FLASH->CR |= FLASH_CR_STRT;
  }
}

void FLASH_ErasePage_AsyncStop(void)
{
  {
    /* Disable the PER Bit */
    FLASH->CR &= ~FLASH_CR_PER;
  }
}

