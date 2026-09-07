#include "main.h"

#include "bootloader.h"

FDCAN_HandleTypeDef hfdcan1;

static void system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
    {
        Error_Handler();
    }

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSIDiv = RCC_HSI_DIV1;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL.PLLM = RCC_PLLM_DIV1;
    oscillator.PLL.PLLN = 8U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = RCC_PLLQ_DIV2;
    oscillator.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        Error_Handler();
    }

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                      LED_BOOTLOADER_Pin,
                      GPIO_PIN_RESET);
    gpio.Pin = LED_BOOTLOADER_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_BOOTLOADER_GPIO_Port, &gpio);
}

static void fdcan_init(void)
{
    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = ENABLE;
    hfdcan1.Init.TransmitPause = ENABLE;
    hfdcan1.Init.ProtocolException = DISABLE;

    /* PCLK1=64 MHz, prescaler=8 and 16 TQ => 500 kbit/s, 87.5% sample point. */
    hfdcan1.Init.NominalPrescaler = 8U;
    hfdcan1.Init.NominalSyncJumpWidth = 2U;
    hfdcan1.Init.NominalTimeSeg1 = 13U;
    hfdcan1.Init.NominalTimeSeg2 = 2U;

    /* Required by HAL even though classic CAN does not use the data phase. */
    hfdcan1.Init.DataPrescaler = 1U;
    hfdcan1.Init.DataSyncJumpWidth = 1U;
    hfdcan1.Init.DataTimeSeg1 = 1U;
    hfdcan1.Init.DataTimeSeg2 = 1U;
    hfdcan1.Init.StdFiltersNbr = 0U;
    hfdcan1.Init.ExtFiltersNbr = 3U;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
    {
        Error_Handler();
    }
}

int main(void)
{
    HAL_Init();
    system_clock_config();
    gpio_init();
    fdcan_init();
    bootloader_init();

    for (;;)
    {
        bootloader_task();
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
    }
}
