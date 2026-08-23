#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_can.h"
#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_device_config.h"
#include "boot_flash.h"
#include "boot_j1939.h"
#include "boot_j1939_address_claim.h"
#include "boot_j1939_tp.h"
#include "boot_metadata.h"
#include "boot_platform.h"
#include "boot_protocol.h"
#include "stm32f1xx.h"

#define LED_PIN                    13U
#define LED_PIN_MASK               (1UL << LED_PIN)
#define SYSTICK_HZ                 1000UL
#define CAN_INIT_TIMEOUT           UINT32_C(1000000)
#define FLASH_OPERATION_TIMEOUT    UINT32_C(1000000)
#define APP_TX_QUEUE_CAPACITY      8U
#define BOOT_REQUEST_MAGIC_LOW     UINT16_C(0xB007)
#define BOOT_REQUEST_MAGIC_HIGH    UINT16_C(0x4C44)
#define RESET_ACK_DELAY_MS         UINT32_C(50)
#define RESET_FORCE_DELAY_MS       UINT32_C(250)
#define BXCAN_EXTENDED_SHIFT       3U
#define BXCAN_IDE_BIT              UINT32_C(0x04)
#define BXCAN_RTR_BIT              UINT32_C(0x02)

typedef enum
{
    BLINK_FIRST_ON,
    BLINK_FIRST_OFF,
    BLINK_SECOND_ON,
    BLINK_PAUSE
} blink_phase_t;

static volatile uint32_t system_tick_ms;
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

void *memset(void *destination, int value, size_t length)
{
    uint8_t *bytes = destination;
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        bytes[index] = (uint8_t)value;
    }
    return destination;
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void led_on(void)
{
    GPIOC->BSRR = LED_PIN_MASK << 16U;
}

static void led_off(void)
{
    GPIOC->BSRR = LED_PIN_MASK;
}

uint32_t boot_platform_uid_word(uint8_t index)
{
    if (index >= 3U)
    {
        return 0U;
    }
    return *(const volatile uint32_t *)(uintptr_t)(UID_BASE +
                                                    ((uint32_t)index * 4U));
}

static bool flash_region(boot_flash_region_t region,
                         uint32_t *base,
                         uint32_t *size)
{
    if ((base == NULL) || (size == NULL))
    {
        return false;
    }
    switch (region)
    {
        case BOOT_FLASH_REGION_APPLICATION:
            *base = BOOT_APPLICATION_ADDRESS;
            *size = BOOT_APPLICATION_MAX_SIZE;
            return true;
        case BOOT_FLASH_REGION_METADATA:
            *base = BOOT_METADATA_ADDRESS;
            *size = BOOT_METADATA_PAGE_SIZE;
            return true;
        case BOOT_FLASH_REGION_DEVICE_CONFIG:
            *base = BOOT_DEVICE_CONFIG_ADDRESS;
            *size = BOOT_DEVICE_CONFIG_PAGE_SIZE;
            return true;
        default:
            return false;
    }
}

bool boot_flash_is_valid_range(boot_flash_region_t region,
                               uint32_t offset,
                               size_t length)
{
    uint32_t base;
    uint32_t size;

    (void)base;
    return flash_region(region, &base, &size) &&
           (offset <= size) &&
           (length <= (size_t)(size - offset));
}

boot_flash_status_t boot_flash_init(void)
{
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_read(boot_flash_region_t region,
                                    uint32_t offset,
                                    uint8_t *data,
                                    size_t length)
{
    uint32_t base;
    uint32_t size;
    size_t index;

    if ((data == NULL) || !flash_region(region, &base, &size) ||
        (offset > size) || (length > (size_t)(size - offset)))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }
    for (index = 0U; index < length; ++index)
    {
        data[index] = *(const volatile uint8_t *)(uintptr_t)(base + offset + index);
    }
    return BOOT_FLASH_STATUS_OK;
}

static bool flash_wait_ready(void)
{
    uint32_t timeout = FLASH_OPERATION_TIMEOUT;

    while (((FLASH->SR & FLASH_SR_BSY) != 0U) && (timeout > 0U))
    {
        --timeout;
    }
    return timeout != 0U;
}

static bool flash_status_ok(void)
{
    return (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) == 0U;
}

static bool flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) == 0U;
}

static void flash_clear_status(void)
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR;
}

boot_flash_status_t boot_flash_erase(boot_flash_region_t region,
                                     uint32_t offset,
                                     size_t length)
{
    uint32_t base;
    uint32_t size;
    uint32_t primask;

    if ((region != BOOT_FLASH_REGION_DEVICE_CONFIG) ||
        !flash_region(region, &base, &size) ||
        (offset != 0U) || (length != size))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (!flash_wait_ready() || !flash_unlock())
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return BOOT_FLASH_STATUS_HARDWARE;
    }
    flash_clear_status();
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = base;
    FLASH->CR |= FLASH_CR_STRT;
    if (!flash_wait_ready() || !flash_status_ok())
    {
        FLASH->CR &= ~FLASH_CR_PER;
        FLASH->CR |= FLASH_CR_LOCK;
        if (primask == 0U)
        {
            __enable_irq();
        }
        return BOOT_FLASH_STATUS_ERASE;
    }
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_LOCK;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return BOOT_FLASH_STATUS_OK;
}

boot_flash_status_t boot_flash_write(boot_flash_region_t region,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     size_t length)
{
    uint32_t base;
    uint32_t size;
    uint32_t primask;
    size_t index;

    if ((region != BOOT_FLASH_REGION_DEVICE_CONFIG) || (data == NULL) ||
        !flash_region(region, &base, &size) ||
        ((offset % BOOT_FLASH_PROGRAM_UNIT) != 0U) ||
        ((length % BOOT_FLASH_PROGRAM_UNIT) != 0U) ||
        (offset > size) || (length > (size_t)(size - offset)))
    {
        return BOOT_FLASH_STATUS_RANGE;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (!flash_wait_ready() || !flash_unlock())
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return BOOT_FLASH_STATUS_HARDWARE;
    }
    flash_clear_status();
    FLASH->CR |= FLASH_CR_PG;
    for (index = 0U; index < length; index += 2U)
    {
        const uint16_t value = (uint16_t)((uint16_t)data[index] |
                                           ((uint16_t)data[index + 1U] << 8U));
        volatile uint16_t *destination =
            (volatile uint16_t *)(uintptr_t)(base + offset + index);

        *destination = value;
        if (!flash_wait_ready() || !flash_status_ok() ||
            (*destination != value))
        {
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            if (primask == 0U)
            {
                __enable_irq();
            }
            return BOOT_FLASH_STATUS_PROGRAM;
        }
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return BOOT_FLASH_STATUS_OK;
}

static bool can_init(void)
{
    uint32_t timeout;

    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;

    GPIOA->CRH = (GPIOA->CRH &
                  ~(GPIO_CRH_MODE11 | GPIO_CRH_CNF11 |
                    GPIO_CRH_MODE12 | GPIO_CRH_CNF12)) |
                 GPIO_CRH_CNF11_1 |
                 GPIO_CRH_MODE12 |
                 GPIO_CRH_CNF12_1;
    GPIOA->BSRR = 1UL << 11U;

    RCC->APB1RSTR |= RCC_APB1RSTR_CAN1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_CAN1RST;

    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_ABOM;
    timeout = CAN_INIT_TIMEOUT;
    while (((CAN1->MSR & CAN_MSR_INAK) == 0U) && (timeout > 0U))
    {
        --timeout;
    }
    if (timeout == 0U)
    {
        return false;
    }

    /* PCLK1=8 MHz, 1 + 13 + 2 TQ, prescaler 1 => 500 kbit/s. */
    CAN1->BTR = (12UL << CAN_BTR_TS1_Pos) |
                (1UL << CAN_BTR_TS2_Pos);

    /* One 32-bit mask accepts extended data frames and rejects STD/RTR. */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1UL;
    CAN1->FM1R &= ~1UL;
    CAN1->FS1R |= 1UL;
    CAN1->FFA1R &= ~1UL;
    CAN1->sFilterRegister[0].FR1 = BXCAN_IDE_BIT;
    CAN1->sFilterRegister[0].FR2 = BXCAN_IDE_BIT | BXCAN_RTR_BIT;
    CAN1->FA1R |= 1UL;
    CAN1->FMR &= ~CAN_FMR_FINIT;

    CAN1->MCR &= ~CAN_MCR_INRQ;
    timeout = CAN_INIT_TIMEOUT;
    while (((CAN1->MSR & CAN_MSR_INAK) != 0U) && (timeout > 0U))
    {
        --timeout;
    }
    return timeout != 0U;
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

static bool can_send_frame(const boot_can_frame_t *frame)
{
    if ((frame == NULL) || (frame->id_type != BOOT_CAN_ID_EXTENDED) ||
        (frame->id > BOOT_CAN_EXTENDED_ID_MAX) ||
        (frame->dlc > BOOT_CAN_MAX_DATA_LENGTH) ||
        ((CAN1->TSR & CAN_TSR_TME0) == 0U))
    {
        return false;
    }

    CAN1->TSR = CAN_TSR_RQCP0;
    CAN1->sTxMailBox[0].TDTR = frame->dlc;
    CAN1->sTxMailBox[0].TDLR = read_u32_le(&frame->data[0]);
    CAN1->sTxMailBox[0].TDHR = read_u32_le(&frame->data[4]);
    CAN1->sTxMailBox[0].TIR =
        (frame->id << BXCAN_EXTENDED_SHIFT) |
        CAN_TI0R_IDE |
        CAN_TI0R_TXRQ;
    return true;
}

static void flush_tx(void)
{
    if ((tx_count > 0U) && can_send_frame(&tx_queue[tx_head]))
    {
        tx_head = (uint8_t)((tx_head + 1U) % APP_TX_QUEUE_CAPACITY);
        --tx_count;
    }
}

static void discard_pending_tx(void)
{
    tx_head = 0U;
    tx_count = 0U;
    if ((CAN1->TSR & CAN_TSR_TME0) == 0U)
    {
        CAN1->TSR = CAN_TSR_ABRQ0;
    }
}

static void apply_claim_address_change(void)
{
    uint8_t changed_address;

    if (boot_j1939_address_claim_take_address_changed(&address_claim,
                                                       &changed_address))
    {
        (void)changed_address;
        discard_pending_tx();
    }
}

static bool can_receive_frame(boot_can_frame_t *frame)
{
    uint32_t rir;
    uint32_t rdtr;
    uint8_t index;

    if ((frame == NULL) || ((CAN1->RF0R & CAN_RF0R_FMP0) == 0U))
    {
        return false;
    }

    rir = CAN1->sFIFOMailBox[0].RIR;
    rdtr = CAN1->sFIFOMailBox[0].RDTR;
    write_u32_le(&frame->data[0], CAN1->sFIFOMailBox[0].RDLR);
    write_u32_le(&frame->data[4], CAN1->sFIFOMailBox[0].RDHR);
    CAN1->RF0R = CAN_RF0R_RFOM0;

    if (((rir & CAN_RI0R_IDE) == 0U) || ((rir & CAN_RI0R_RTR) != 0U) ||
        ((rdtr & CAN_RDT0R_DLC) > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return false;
    }
    frame->id = (rir >> BXCAN_EXTENDED_SHIFT) & BOOT_CAN_EXTENDED_ID_MAX;
    frame->id_type = BOOT_CAN_ID_EXTENDED;
    frame->dlc = (uint8_t)(rdtr & CAN_RDT0R_DLC);
    for (index = frame->dlc; index < BOOT_CAN_MAX_DATA_LENGTH; ++index)
    {
        frame->data[index] = UINT8_C(0xFF);
    }
    return true;
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

static void handle_control(const boot_can_frame_t *frame)
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
                boot_reset_deadline = system_tick_ms + RESET_ACK_DELAY_MS;
                boot_reset_force_deadline = system_tick_ms + RESET_FORCE_DELAY_MS;
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

static void process_commanded_address(void)
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
    if (!boot_j1939_address_claim_restart(&address_claim,
                                          new_address,
                                          system_tick_ms))
    {
        return;
    }
    apply_claim_address_change();
    collect_claim_tx();
}

static void handle_tp(const boot_can_frame_t *frame)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool response_ready;

    if (!boot_j1939_tp_rx_handle(&tp_receiver,
                                 frame,
                                 system_tick_ms,
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
        process_commanded_address();
    }
}

static void service_tp(void)
{
    boot_can_frame_t response;
    boot_j1939_tp_event_t event;
    bool response_ready;

    boot_j1939_tp_rx_task(&tp_receiver,
                          system_tick_ms,
                          &response,
                          &response_ready,
                          &event);
    if (response_ready)
    {
        (void)queue_frame(&response);
    }
}

static void dispatch_can(const boot_can_frame_t *frame)
{
    boot_j1939_header_t header;

    (void)boot_j1939_address_claim_handle(&address_claim,
                                           frame,
                                           system_tick_ms);
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
        handle_tp(frame);
        return;
    }
    if (boot_j1939_address_claim_is_operational(&address_claim) &&
        (header.pgn == BOOT_J1939_PGN_PROPRIETARY_A) &&
        header.destination_specific &&
        (header.destination_address == local_address()))
    {
        handle_control(frame);
    }
}

static void request_bootloader_reset(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR->CR |= PWR_CR_DBP;
    (void)PWR->CR;
    BKP->DR1 = BOOT_REQUEST_MAGIC_LOW;
    BKP->DR2 = BOOT_REQUEST_MAGIC_HIGH;
    __DSB();
    NVIC_SystemReset();
    for (;;)
    {
    }
}

static void service_network(void)
{
    boot_can_frame_t frame;

    boot_j1939_address_claim_task(&address_claim, system_tick_ms);
    apply_claim_address_change();
    collect_claim_tx();
    service_tp();
    flush_tx();
    while (can_receive_frame(&frame))
    {
        dispatch_can(&frame);
        if (tx_count >= APP_TX_QUEUE_CAPACITY)
        {
            break;
        }
    }
    flush_tx();
}

void SysTick_Handler(void)
{
    ++system_tick_ms;
}

int main(void)
{
    blink_phase_t phase;
    uint32_t deadline;
    bool can_ready;

    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    (void)RCC->APB2ENR;
    led_off();
    GPIOC->CRH = (GPIOC->CRH & ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13)) |
                 GPIO_CRH_MODE13_1;

    if (__get_PRIMASK() != 0U)
    {
        led_on();
        for (;;)
        {
            __NOP();
        }
    }

    boot_device_config_load(&device_config);
    boot_j1939_address_claim_init(&address_claim,
                                  device_config.name,
                                  device_config.preferred_address);
    boot_j1939_tp_rx_init(&tp_receiver);
    (void)boot_j1939_tp_rx_allow_bam(&tp_receiver,
                                     BOOT_J1939_PGN_COMMANDED_ADDRESS,
                                     commanded_address_buffer,
                                     sizeof(commanded_address_buffer));
    can_ready = can_init();
    if (can_ready)
    {
        can_ready = boot_j1939_address_claim_start(&address_claim,
                                                   system_tick_ms);
        apply_claim_address_change();
        collect_claim_tx();
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
        const uint32_t now = system_tick_ms;

        if (can_ready)
        {
            service_network();
        }
        if (boot_reset_pending &&
            ((((CAN1->TSR & (CAN_TSR_RQCP0 | CAN_TSR_TXOK0)) ==
               (CAN_TSR_RQCP0 | CAN_TSR_TXOK0)) &&
              (tx_count == 0U) &&
              ((int32_t)(now - boot_reset_deadline) >= 0)) ||
             ((int32_t)(now - boot_reset_force_deadline) >= 0)))
        {
            request_bootloader_reset();
        }

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
