#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_device_config_format.h"
#include "boot_protocol.h"
#include "stm32f1xx.h"

#define LED_PIN                   13U
#define LED_PIN_MASK              (1UL << LED_PIN)
#define SYSTICK_HZ                1000UL
#define CAN_INIT_TIMEOUT          UINT32_C(1000000)
#define BOOT_REQUEST_MAGIC_LOW    UINT16_C(0xB007)
#define BOOT_REQUEST_MAGIC_HIGH   UINT16_C(0x4C44)
#define RESET_ACK_DELAY_MS        UINT32_C(50)
#define RESET_FORCE_DELAY_MS      UINT32_C(250)

static volatile uint32_t system_tick_ms;
static uint8_t application_node_id;
static bool boot_reset_pending;
static uint32_t boot_reset_deadline;
static uint32_t boot_reset_force_deadline;

typedef enum
{
    BLINK_FIRST_ON,
    BLINK_FIRST_OFF,
    BLINK_SECOND_ON,
    BLINK_PAUSE
} blink_phase_t;

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
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

static uint8_t load_node_id(void)
{
    uint8_t raw[BOOT_DEVICE_CONFIG_SERIALIZED_SIZE];
    const volatile uint8_t *source =
        (const volatile uint8_t *)(uintptr_t)BOOT_DEVICE_CONFIG_ADDRESS;
    uint8_t node_id;
    size_t index;

    for (index = 0U; index < sizeof(raw); ++index)
    {
        raw[index] = source[index];
    }

    node_id = raw[BOOT_DEVICE_CONFIG_NODE_OFFSET];
    if ((read_u32_le(&raw[BOOT_DEVICE_CONFIG_MAGIC_OFFSET]) !=
         BOOT_DEVICE_CONFIG_MAGIC) ||
        (read_u16_le(&raw[BOOT_DEVICE_CONFIG_FORMAT_OFFSET]) !=
         BOOT_DEVICE_CONFIG_FORMAT_VERSION) ||
        (node_id < BOOT_MIN_NODE_ID) || (node_id > BOOT_MAX_NODE_ID) ||
        ((uint8_t)(node_id ^ raw[BOOT_DEVICE_CONFIG_NODE_INV_OFFSET]) != UINT8_MAX) ||
        (boot_crc32_compute(raw, BOOT_DEVICE_CONFIG_CRC_INPUT_SIZE) !=
         read_u32_le(&raw[BOOT_DEVICE_CONFIG_CRC_OFFSET])))
    {
        return BOOT_DEFAULT_NODE_ID;
    }

    return node_id;
}

static void led_on(void)
{
    /* PC13 is active-low on the target board. */
    GPIOC->BSRR = LED_PIN_MASK << 16U;
}

static void led_off(void)
{
    GPIOC->BSRR = LED_PIN_MASK;
}

static uint32_t filter_value(uint32_t standard_id)
{
    return standard_id << 5U;
}

static bool can_init(uint8_t node_id)
{
    uint32_t timeout;
    const uint32_t command_filter = filter_value(BOOT_CAN_COMMAND_ID(node_id));
    const uint32_t broadcast_filter = filter_value(BOOT_CAN_BROADCAST_ID);

    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;

    /* PA11: CAN_RX input pull-up. PA12: CAN_TX alternate push-pull, 50 MHz. */
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

    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1UL;
    CAN1->FM1R |= 1UL;   /* Identifier list. */
    CAN1->FS1R &= ~1UL;  /* Four 16-bit identifiers. */
    CAN1->FFA1R &= ~1UL;
    CAN1->sFilterRegister[0].FR1 = command_filter |
                                          (command_filter << 16U);
    CAN1->sFilterRegister[0].FR2 = broadcast_filter |
                                          (broadcast_filter << 16U);
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

static bool can_send_response(boot_command_t command,
                              uint8_t session,
                              uint32_t value)
{
    uint8_t data[8];

    if ((CAN1->TSR & CAN_TSR_TME0) == 0U)
    {
        return false;
    }

    data[0] = BOOT_PROTOCOL_VERSION;
    data[1] = (uint8_t)BOOT_RESPONSE_ACK;
    data[2] = (uint8_t)command;
    data[3] = session;
    write_u32_le(&data[4], value);

    CAN1->TSR = CAN_TSR_RQCP0;
    CAN1->sTxMailBox[0].TDTR = 8U;
    CAN1->sTxMailBox[0].TDLR = read_u32_le(&data[0]);
    CAN1->sTxMailBox[0].TDHR = read_u32_le(&data[4]);
    CAN1->sTxMailBox[0].TIR =
        (BOOT_CAN_RESPONSE_ID(application_node_id) << CAN_TI0R_STID_Pos) |
        CAN_TI0R_TXRQ;
    return true;
}

static uint32_t probe_value(uint8_t selector, bool *valid)
{
    *valid = true;
    switch ((boot_probe_selector_t)selector)
    {
        case BOOT_PROBE_CAPABILITIES:
            return BOOT_CAPABILITY_APPLICATION_VALID |
                   BOOT_CAPABILITY_APPLICATION_RUNNING |
                   ((uint32_t)application_node_id << 8U) |
                   ((uint32_t)BOOT_PROTOCOL_VERSION << 16U) |
                   ((uint32_t)BOOT_TARGET_CODE << 24U);

        case BOOT_PROBE_UID_WORD_0:
            return *(const volatile uint32_t *)(uintptr_t)(UID_BASE + 0U);

        case BOOT_PROBE_UID_WORD_1:
            return *(const volatile uint32_t *)(uintptr_t)(UID_BASE + 4U);

        case BOOT_PROBE_UID_WORD_2:
            return *(const volatile uint32_t *)(uintptr_t)(UID_BASE + 8U);

        default:
            *valid = false;
            return 0U;
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

static void service_can(void)
{
    uint32_t rir;
    uint32_t rdtr;
    uint32_t rdlr;
    uint32_t rdhr;
    uint32_t frame_id;
    uint8_t data[8];
    uint8_t session;
    bool selector_valid;
    uint32_t value;

    if ((CAN1->RF0R & CAN_RF0R_FMP0) == 0U)
    {
        return;
    }

    rir = CAN1->sFIFOMailBox[0].RIR;
    rdtr = CAN1->sFIFOMailBox[0].RDTR;
    rdlr = CAN1->sFIFOMailBox[0].RDLR;
    rdhr = CAN1->sFIFOMailBox[0].RDHR;
    CAN1->RF0R = CAN_RF0R_RFOM0;

    if (((rir & (CAN_RI0R_IDE | CAN_RI0R_RTR)) != 0U) ||
        ((rdtr & CAN_RDT0R_DLC) != 8U))
    {
        return;
    }

    frame_id = (rir & CAN_RI0R_STID) >> CAN_RI0R_STID_Pos;
    write_u32_le(&data[0], rdlr);
    write_u32_le(&data[4], rdhr);
    session = data[2];
    if ((data[0] != BOOT_PROTOCOL_VERSION) || (session == 0U))
    {
        return;
    }

    if (((boot_command_t)data[1] == BOOT_COMMAND_PROBE) &&
        ((frame_id == BOOT_CAN_BROADCAST_ID) ||
         (frame_id == BOOT_CAN_COMMAND_ID(application_node_id))) &&
        (read_u32_le(&data[4]) == 0U))
    {
        value = probe_value(data[3], &selector_valid);
        if (selector_valid)
        {
            (void)can_send_response(BOOT_COMMAND_PROBE, session, value);
        }
    }
    else if (((boot_command_t)data[1] == BOOT_COMMAND_ENTER_BOOTLOADER) &&
             (frame_id == BOOT_CAN_COMMAND_ID(application_node_id)) &&
             (data[3] == 0U) && (read_u32_le(&data[4]) == 0U) &&
             can_send_response(BOOT_COMMAND_ENTER_BOOTLOADER, session, 0U))
    {
        boot_reset_pending = true;
        boot_reset_deadline = system_tick_ms + RESET_ACK_DELAY_MS;
        boot_reset_force_deadline = system_tick_ms + RESET_FORCE_DELAY_MS;
    }
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

    /* Keep the LED off while PC13 changes from input to output. */
    led_off();
    GPIOC->CRH = (GPIOC->CRH & ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13)) |
                 GPIO_CRH_MODE13_1;

    /* Do not repair the hand-off here: masked IRQs must remain observable. */
    if (__get_PRIMASK() != 0U)
    {
        led_on();
        for (;;)
        {
            __NOP();
        }
    }

    application_node_id = load_node_id();
    can_ready = can_init(application_node_id);

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

        if (can_ready)
        {
            service_can();
        }
        if (boot_reset_pending &&
            ((((CAN1->TSR & (CAN_TSR_RQCP0 | CAN_TSR_TXOK0)) ==
               (CAN_TSR_RQCP0 | CAN_TSR_TXOK0)) &&
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
