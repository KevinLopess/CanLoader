#ifndef CANLOADER_STM32G0B1RE_MAIN_H
#define CANLOADER_STM32G0B1RE_MAIN_H

#include "stm32g0xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern FDCAN_HandleTypeDef hfdcan1;

void Error_Handler(void);

#define LED_BOOTLOADER_Pin       GPIO_PIN_5
#define LED_BOOTLOADER_GPIO_Port GPIOA

#define CANLOADER_FDCAN_RX_Pin       GPIO_PIN_4
#define CANLOADER_FDCAN_RX_GPIO_Port GPIOC
#define CANLOADER_FDCAN_TX_Pin       GPIO_PIN_5
#define CANLOADER_FDCAN_TX_GPIO_Port GPIOC

#ifdef __cplusplus
}
#endif

#endif /* CANLOADER_STM32G0B1RE_MAIN_H */
