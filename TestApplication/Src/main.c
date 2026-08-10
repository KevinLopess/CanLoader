#include <stdint.h>

#include "stm32f1xx.h"

#define LED_PIN          13U
#define LED_PIN_MASK     (1UL << LED_PIN)
#define SYSTICK_HZ       1000UL

static volatile uint32_t system_tick_ms;

typedef enum
{
  BLINK_FIRST_ON,
  BLINK_FIRST_OFF,
  BLINK_SECOND_ON,
  BLINK_PAUSE
} blink_phase_t;

static void led_on(void)
{
  /* PC13 is active-low on the target board. */
  GPIOC->BSRR = LED_PIN_MASK << 16U;
}

static void led_off(void)
{
  GPIOC->BSRR = LED_PIN_MASK;
}

void SysTick_Handler(void)
{
  ++system_tick_ms;
}

int main(void)
{
  blink_phase_t phase;
  uint32_t deadline;

  RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
  (void)RCC->APB2ENR;

  /* Keep the LED off while PC13 changes from input to output. */
  led_off();
  GPIOC->CRH = (GPIOC->CRH & ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13)) |
               GPIO_CRH_MODE13_1; /* General-purpose push-pull, 2 MHz. */

  /* Do not repair the hand-off here: masked IRQs must remain observable. */
  if (__get_PRIMASK() != 0U)
  {
    led_on();
    for (;;)
    {
      __NOP(); /* Steady LED is the explicit hand-off error indication. */
    }
  }

  SysTick->LOAD = (SystemCoreClock / SYSTICK_HZ) - 1UL;
  SysTick->VAL = 0UL;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                  SysTick_CTRL_TICKINT_Msk |
                  SysTick_CTRL_ENABLE_Msk;

  phase = BLINK_FIRST_ON;
  led_on();
  deadline = system_tick_ms + 80U;

  for (;;)
  {
    uint32_t now = system_tick_ms;

    if ((int32_t)(now - deadline) >= 0)
    {
      switch (phase)
      {
        case BLINK_FIRST_ON:
          led_off();
          phase = BLINK_FIRST_OFF;
          deadline += 100U;
          break;

        case BLINK_FIRST_OFF:
          led_on();
          phase = BLINK_SECOND_ON;
          deadline += 80U;
          break;

        case BLINK_SECOND_ON:
          led_off();
          phase = BLINK_PAUSE;
          deadline += 740U;
          break;

        case BLINK_PAUSE:
        default:
          led_on();
          phase = BLINK_FIRST_ON;
          deadline += 80U;
          break;
      }
    }

    __WFI();
  }
}
