#include <stdint.h>

#include "stm32f1xx.h"

#define APPLICATION_VECTOR_TABLE 0x08005000UL
#define APPLICATION_HSI_HZ UINT32_C(8000000)

uint32_t SystemCoreClock = APPLICATION_HSI_HZ;

const uint8_t AHBPrescTable[16U] =
{
  0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
  1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U
};

const uint8_t APBPrescTable[8U] =
{
  0U, 0U, 0U, 0U, 1U, 2U, 3U, 4U
};

void SystemInit(void)
{
  /* This runs from Reset_Handler, before .data and .bss initialization. */
  SCB->VTOR = APPLICATION_VECTOR_TABLE;
  __DSB();
  __ISB();

  /* Also make a bootloader hand-off deterministic: return SYSCLK to HSI. */
  RCC->CR |= RCC_CR_HSION;
  while ((RCC->CR & RCC_CR_HSIRDY) == 0U)
  {
  }

  RCC->CFGR &= ~RCC_CFGR_SW;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
  {
  }

  RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON | RCC_CR_HSEON);
  while ((RCC->CR & (RCC_CR_PLLRDY | RCC_CR_HSERDY)) != 0U)
  {
  }
  RCC->CR &= ~RCC_CR_HSEBYP;

  RCC->CFGR = 0UL;
  RCC->CIR = 0x009F0000UL;
  SystemCoreClock = APPLICATION_HSI_HZ;
}

void SystemCoreClockUpdate(void)
{
  /* SystemInit fixes HCLK at the undivided 8 MHz HSI clock. */
  SystemCoreClock = APPLICATION_HSI_HZ;
}

/* The reused ST startup calls this hook. This C-only image has no constructors. */
void __libc_init_array(void)
{
}
