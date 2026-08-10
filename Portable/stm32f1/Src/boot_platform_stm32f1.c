#include "boot_platform.h"

#include "boot_config.h"
#include "main.h"

uint32_t boot_platform_millis(void)
{
    return HAL_GetTick();
}

void boot_platform_led_set(bool on)
{
#if BOOT_LED_ACTIVE_LOW
    const GPIO_PinState state = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
    const GPIO_PinState state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port, LED_BOOTLOADER_Pin, state);
}

void boot_platform_prepare_handoff(void)
{
    uint32_t index;

    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (index = 0U; index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++index)
    {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }

    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    __DSB();
    __ISB();
}

__attribute__((naked, noreturn)) void boot_platform_jump(
    uint32_t vector_address __attribute__((unused)),
    uint32_t initial_stack_pointer __attribute__((unused)),
    uint32_t reset_handler __attribute__((unused)))
{
    __asm volatile(
        "ldr r3, =0xE000ED08\n"
        "str r0, [r3]\n"
        "dsb\n"
        "isb\n"
        "movs r0, #0\n"
        "msr control, r0\n"
        "msr msp, r1\n"
        "msr basepri, r0\n"
        "msr faultmask, r0\n"
        "msr primask, r0\n"
        "isb\n"
        "bx r2\n");
}

void boot_platform_reset(void)
{
    NVIC_SystemReset();
    for (;;)
    {
    }
}
