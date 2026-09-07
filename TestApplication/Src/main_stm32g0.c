#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_j1939.h"
#include "boot_j1939_address_claim.h"
#include "boot_j1939_tp.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"
#include "main.h"

#define APP_TX_QUEUE_CAPACITY 8U
#define RESET_ACK_DELAY_MS    UINT32_C(50)
#define RESET_FORCE_DELAY_MS  UINT32_C(250)

FDCAN_HandleTypeDef hfdcan1;

typedef enum
{
    BLINK_FIRST_ON,
    BLINK_FIRST_OFF,
    BLINK_SECOND_ON,
    BLINK_PAUSE
} blink_phase_t;

static boot_device_config_t device_config;
static boot_j1939_address_claim_t address_claim;
static boot_j1939_tp_rx_t tp_receiver;
static boot_can_frame_t tx_queue[APP_TX_QUEUE_CAPACITY];
static uint8_t commanded_address_buffer[9];
static uint8_t tx_head;
static uint8_t tx_count;
static bool boot_reset_pending;
static uint32_t boot_reset_deadline;
static uint32_t boot_reset_force_deadline;

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
    hfdcan1.Init.NominalPrescaler = 8U;
    hfdcan1.Init.NominalSyncJumpWidth = 2U;
    hfdcan1.Init.NominalTimeSeg1 = 13U;
    hfdcan1.Init.NominalTimeSeg2 = 2U;
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

static bool queue_frame(const boot_can_frame_t *frame)
{
    uint8_t tail;

    if ((frame == NULL) || (tx_count >= APP_TX_QUEUE_CAPACITY))
    {
        return false;
    }
    tail = (uint8_t)((tx_head + tx_count) % APP_TX_QUEUE_CAPACITY);
    tx_queue[tail] = *frame;
    ++tx_count;
    return true;
}

static void flush_tx(void)
{
    if ((tx_count > 0U) &&
        (boot_can_send(&tx_queue[tx_head]) == BOOT_CAN_STATUS_OK))
    {
        tx_head = (uint8_t)((tx_head + 1U) % APP_TX_QUEUE_CAPACITY);
        --tx_count;
    }
}

static void discard_pending_tx(void)
{
    tx_head = 0U;
    tx_count = 0U;
    (void)boot_can_abort_pending_tx();
}

static void apply_claim_address_change(void)
{
    uint8_t changed_address;

    if (boot_j1939_address_claim_take_address_changed(&address_claim,
                                                       &changed_address))
    {
        discard_pending_tx();
        (void)boot_can_configure_address(changed_address);
    }
}

static uint8_t local_address(void)
{
    return boot_j1939_address_claim_source_address(&address_claim);
}

static void collect_claim_tx(void)
{
    boot_can_frame_t frame;

    while ((tx_count < APP_TX_QUEUE_CAPACITY) &&
           boot_j1939_address_claim_take_tx(&address_claim, &frame))
    {
        (void)queue_frame(&frame);
    }
}

static uint32_t capability_word(void)
{
    return BOOT_CAPABILITY_APPLICATION_VALID |
           BOOT_CAPABILITY_APPLICATION_RUNNING |
           BOOT_CAPABILITY_J1939 |
           BOOT_CAPABILITY_ADDRESS_CLAIM |
           BOOT_CAPABILITY_TP_RX |
           ((uint32_t)local_address() << 8U) |
           ((uint32_t)BOOT_PROTOCOL_VERSION << 16U) |
           ((uint32_t)BOOT_TARGET_CODE << 24U);
}

static bool info_value(uint8_t selector, uint32_t *value)
{
    boot_metadata_t metadata;
    const bool metadata_valid = boot_metadata_read(&metadata) &&
                                (metadata.state == BOOT_METADATA_STATE_VALID);

    if (value == NULL)
    {
        return false;
    }
    switch ((boot_info_selector_t)selector)
    {
        case BOOT_INFO_CAPABILITIES:
            *value = capability_word();
            return true;
        case BOOT_INFO_APPLICATION_BASE:
            *value = BOOT_APPLICATION_ADDRESS;
            return true;
        case BOOT_INFO_APPLICATION_MAX_SIZE:
            *value = BOOT_APPLICATION_MAX_SIZE;
            return true;
        case BOOT_INFO_STORED_IMAGE_SIZE:
            *value = metadata_valid ? metadata.image_size : 0U;
            return true;
        case BOOT_INFO_STORED_IMAGE_CRC:
            *value = metadata_valid ? metadata.image_crc32 : 0U;
            return true;
        case BOOT_INFO_UID_WORD_0:
            *value = boot_platform_uid_word(0U);
            return true;
        case BOOT_INFO_UID_WORD_1:
            *value = boot_platform_uid_word(1U);
            return true;
        case BOOT_INFO_UID_WORD_2:
            *value = boot_platform_uid_word(2U);
            return true;
        case BOOT_INFO_NAME_LOW:
            *value = (uint32_t)device_config.name;
            return true;
        case BOOT_INFO_NAME_HIGH:
            *value = (uint32_t)(device_config.name >> 32U);
            return true;
        case BOOT_INFO_SOURCE_ADDRESS:
            *value = local_address();
            return true;
        case BOOT_INFO_FLASH_ERASE_UNIT_SIZE:
            *value = BOOT_FLASH_PAGE_SIZE;
            return true;
        default:
            return false;
    }
}

static void queue_control_response(const boot_protocol_request_t *request,
                                   boot_response_kind_t kind,
                                   uint32_t value)
{
    boot_can_frame_t frame;

    if (boot_protocol_make_response(&frame,
                                    local_address(),
                                    request->source_address,
                                    kind,
                                    request->command,
                                    request->session,
                                    value))
    {
        (void)queue_frame(&frame);
    }
}

static void handle_control(const boot_can_frame_t *frame, uint32_t now)
{
    boot_protocol_request_t request;
    boot_error_t error;
    uint32_t value;

    if (!boot_protocol_decode_request(frame, &request, &error) ||
        (request.destination_address != local_address()))
    {
        return;
    }
    if (request.session == 0U)
    {
        queue_control_response(&request, BOOT_RESPONSE_NACK, BOOT_ERROR_SESSION);
        return;
    }

    switch (request.command)
    {
        case BOOT_COMMAND_IDENTIFY:
            if ((request.parameter == 0U) && (request.value == 0U))
            {
                queue_control_response(&request,
                                       BOOT_RESPONSE_ACK,
                                       capability_word());
            }
            else
            {
                queue_control_response(&request,
                                       BOOT_RESPONSE_NACK,
                                       BOOT_ERROR_MALFORMED_FRAME);
            }
            break;
        case BOOT_COMMAND_INFO:
            if ((request.value == 0U) && info_value(request.parameter, &value))
            {
                queue_control_response(&request, BOOT_RESPONSE_ACK, value);
            }
            else
            {
                queue_control_response(&request,
                                       BOOT_RESPONSE_NACK,
                                       BOOT_ERROR_INVALID_COMMAND);
            }
            break;
        case BOOT_COMMAND_ENTER_BOOTLOADER:
            if ((request.parameter == 0U) && (request.value == 0U))
            {
                queue_control_response(&request, BOOT_RESPONSE_ACK, 0U);
                boot_reset_pending = true;
                boot_reset_deadline = now + RESET_ACK_DELAY_MS;
                boot_reset_force_deadline = now + RESET_FORCE_DELAY_MS;
            }
            else
            {
                queue_control_response(&request,
                                       BOOT_RESPONSE_NACK,
                                       BOOT_ERROR_MALFORMED_FRAME);
            }
            break;
        default:
            queue_control_response(&request,
                                   BOOT_RESPONSE_NACK,
                                   BOOT_ERROR_INVALID_STATE);
            break;
    }
}

static void process_commanded_address(uint32_t now)
{
    const uint8_t *payload = boot_j1939_tp_rx_data(&tp_receiver);
    const uint16_t size = boot_j1939_tp_rx_size(&tp_receiver);
    uint8_t new_address;

    if ((payload == NULL) || (size != 9U) ||
        (boot_protocol_read_u64_le(payload) != device_config.name))
    {
        return;
    }
    new_address = payload[8];
    if ((new_address >= BOOT_J1939_NULL_ADDRESS) ||
        (boot_device_config_store_preferred_address(new_address) !=
         BOOT_FLASH_STATUS_OK))
    {
        return;
    }
    boot_device_config_load(&device_config);
    if (boot_j1939_address_claim_restart(&address_claim, new_address, now))
    {
        apply_claim_address_change();
        collect_claim_tx();
    }
}

static void handle_tp(const boot_can_frame_t *frame, uint32_t now)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool response_ready;

    if (!boot_j1939_tp_rx_handle(&tp_receiver,
                                 frame,
                                 now,
                                 &response,
                                 &response_ready,
                                 &event))
    {
        return;
    }
    if (response_ready)
    {
        (void)queue_frame(&response);
    }
    if ((event == BOOT_J1939_TP_EVENT_COMPLETE) &&
        (boot_j1939_tp_rx_pgn(&tp_receiver) ==
         BOOT_J1939_PGN_COMMANDED_ADDRESS))
    {
        process_commanded_address(now);
    }
}

static void service_tp(uint32_t now)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool response_ready;

    boot_j1939_tp_rx_task(&tp_receiver,
                          now,
                          &response,
                          &response_ready,
                          &event);
    if (response_ready)
    {
        (void)queue_frame(&response);
    }
}

static void dispatch_can(const boot_can_frame_t *frame, uint32_t now)
{
    boot_j1939_header_t header;

    (void)boot_j1939_address_claim_handle(&address_claim, frame, now);
    apply_claim_address_change();
    collect_claim_tx();
    if (!boot_j1939_decode_frame(frame, &header))
    {
        return;
    }
    if ((header.pgn == BOOT_J1939_PGN_ADDRESS_CLAIMED) ||
        (header.pgn == BOOT_J1939_PGN_REQUEST))
    {
        return;
    }
    if ((header.pgn == BOOT_J1939_PGN_TP_CM) ||
        (header.pgn == BOOT_J1939_PGN_TP_DT))
    {
        handle_tp(frame, now);
        return;
    }
    if (boot_j1939_address_claim_is_operational(&address_claim) &&
        (header.pgn == BOOT_J1939_PGN_PROPRIETARY_A) &&
        header.destination_specific &&
        (header.destination_address == local_address()))
    {
        handle_control(frame, now);
    }
}

static void service_network(uint32_t now)
{
    boot_can_frame_t frame;
    uint32_t budget = BOOT_RX_BUDGET_PER_TASK;

    boot_j1939_address_claim_task(&address_claim, now);
    apply_claim_address_change();
    collect_claim_tx();
    service_tp(now);
    flush_tx();
    while ((budget > 0U) &&
           (boot_can_receive(&frame) == BOOT_CAN_STATUS_OK))
    {
        dispatch_can(&frame, now);
        --budget;
    }
    flush_tx();
}

int main(void)
{
    blink_phase_t phase = BLINK_FIRST_ON;
    uint32_t deadline;
    bool can_ready;

    HAL_Init();
    system_clock_config();
    gpio_init();
    fdcan_init();

    if (__get_PRIMASK() != 0U)
    {
        HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                          LED_BOOTLOADER_Pin,
                          GPIO_PIN_SET);
        for (;;)
        {
        }
    }

    (void)boot_flash_init();
    boot_device_config_load(&device_config);
    boot_j1939_address_claim_init(&address_claim,
                                  device_config.name,
                                  device_config.preferred_address);
    boot_j1939_tp_rx_init(&tp_receiver);
    (void)boot_j1939_tp_rx_allow_bam(&tp_receiver,
                                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                     commanded_address_buffer,
                                     sizeof(commanded_address_buffer));
    can_ready = boot_can_init(device_config.preferred_address) &&
                boot_can_start() &&
                boot_j1939_address_claim_start(&address_claim, HAL_GetTick());
    apply_claim_address_change();
    collect_claim_tx();

    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                      LED_BOOTLOADER_Pin,
                      GPIO_PIN_SET);
    deadline = HAL_GetTick() + 80U;

    for (;;)
    {
        const uint32_t now = HAL_GetTick();

        if (can_ready)
        {
            service_network(now);
        }
        if (boot_reset_pending &&
            ((((int32_t)(now - boot_reset_deadline) >= 0) &&
              (tx_count == 0U) && boot_can_tx_idle()) ||
             ((int32_t)(now - boot_reset_force_deadline) >= 0)))
        {
            boot_platform_set_boot_request();
            boot_platform_reset();
        }

        if ((int32_t)(now - deadline) >= 0)
        {
            switch (phase)
            {
                case BLINK_FIRST_ON:
                    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                                      LED_BOOTLOADER_Pin,
                                      GPIO_PIN_RESET);
                    phase = BLINK_FIRST_OFF;
                    deadline += 100U;
                    break;
                case BLINK_FIRST_OFF:
                    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                                      LED_BOOTLOADER_Pin,
                                      GPIO_PIN_SET);
                    phase = BLINK_SECOND_ON;
                    deadline += 80U;
                    break;
                case BLINK_SECOND_ON:
                    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                                      LED_BOOTLOADER_Pin,
                                      GPIO_PIN_RESET);
                    phase = BLINK_PAUSE;
                    deadline += 740U;
                    break;
                case BLINK_PAUSE:
                default:
                    HAL_GPIO_WritePin(LED_BOOTLOADER_GPIO_Port,
                                      LED_BOOTLOADER_Pin,
                                      GPIO_PIN_SET);
                    phase = BLINK_FIRST_ON;
                    deadline += 80U;
                    break;
            }
        }
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

/* The freestanding reference application has no C++ constructors. */
void __libc_init_array(void)
{
}
