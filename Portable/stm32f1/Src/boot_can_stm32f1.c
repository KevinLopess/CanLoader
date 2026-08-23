#include "boot_can.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "main.h"

extern CAN_HandleTypeDef hcan;

#define BXCAN_EXTENDED_SHIFT 3U
#define BXCAN_IDE_BIT        UINT32_C(0x04)
#define BXCAN_RTR_BIT        UINT32_C(0x02)
#define J1939_DA_ID_MASK     (UINT32_C(0xFF) << 8U)

static void split_filter_word(uint32_t value,
                              uint32_t *high,
                              uint32_t *low)
{
    *high = (value >> 16U) & UINT32_C(0xFFFF);
    *low = value & UINT32_C(0xFFFF);
}

static bool configure_destination_filter(uint32_t bank, uint8_t address)
{
    CAN_FilterTypeDef filter = {0};
    const uint32_t value =
        (((uint32_t)address << 8U) << BXCAN_EXTENDED_SHIFT) | BXCAN_IDE_BIT;
    const uint32_t mask =
        (J1939_DA_ID_MASK << BXCAN_EXTENDED_SHIFT) |
        BXCAN_IDE_BIT |
        BXCAN_RTR_BIT;

    filter.FilterBank = bank;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    split_filter_word(value, &filter.FilterIdHigh, &filter.FilterIdLow);
    split_filter_word(mask, &filter.FilterMaskIdHigh, &filter.FilterMaskIdLow);
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;
    return HAL_CAN_ConfigFilter(&hcan, &filter) == HAL_OK;
}

bool boot_can_configure_address(uint8_t local_address)
{
    if (local_address > BOOT_J1939_NULL_ADDRESS)
    {
        return false;
    }

    return configure_destination_filter(0U, local_address) &&
           configure_destination_filter(1U, BOOT_J1939_GLOBAL_ADDRESS);
}

bool boot_can_init(uint8_t local_address)
{
    return boot_can_configure_address(local_address);
}

bool boot_can_abort_pending_tx(void)
{
    return HAL_CAN_AbortTxRequest(&hcan,
                                  CAN_TX_MAILBOX0 |
                                  CAN_TX_MAILBOX1 |
                                  CAN_TX_MAILBOX2) == HAL_OK;
}

bool boot_can_start(void)
{
    return HAL_CAN_Start(&hcan) == HAL_OK;
}

void boot_can_stop(void)
{
    if (HAL_CAN_GetState(&hcan) == HAL_CAN_STATE_LISTENING)
    {
        (void)HAL_CAN_Stop(&hcan);
    }
    (void)HAL_CAN_DeInit(&hcan);
}

boot_can_status_t boot_can_send(const boot_can_frame_t *frame)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;

    if ((frame == NULL) || (frame->id_type != BOOT_CAN_ID_EXTENDED) ||
        (frame->id > BOOT_CAN_EXTENDED_ID_MAX) ||
        (frame->dlc > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return BOOT_CAN_STATUS_ERROR;
    }

    if ((HAL_CAN_GetError(&hcan) & HAL_CAN_ERROR_BOF) != 0U)
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0U)
    {
        return BOOT_CAN_STATUS_BUSY;
    }

    header.StdId = 0U;
    header.ExtId = frame->id;
    header.IDE = CAN_ID_EXT;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->dlc;
    header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan,
                             &header,
                             (uint8_t *)frame->data,
                             &mailbox) != HAL_OK)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_receive(boot_can_frame_t *frame)
{
    CAN_RxHeaderTypeDef header = {0};
    uint8_t index;

    if (frame == NULL)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    if ((HAL_CAN_GetError(&hcan) & HAL_CAN_ERROR_BOF) != 0U)
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0U)
    {
        return BOOT_CAN_STATUS_EMPTY;
    }
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &header, frame->data) != HAL_OK)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    if ((header.IDE != CAN_ID_EXT) || (header.RTR != CAN_RTR_DATA) ||
        (header.ExtId > BOOT_CAN_EXTENDED_ID_MAX) ||
        (header.DLC > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return BOOT_CAN_STATUS_ERROR;
    }

    frame->id = header.ExtId;
    frame->id_type = BOOT_CAN_ID_EXTENDED;
    frame->dlc = (uint8_t)header.DLC;
    for (index = frame->dlc; index < BOOT_CAN_MAX_DATA_LENGTH; ++index)
    {
        frame->data[index] = UINT8_C(0xFF);
    }
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_get_state(void)
{
    if ((HAL_CAN_GetError(&hcan) & HAL_CAN_ERROR_BOF) != 0U)
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    if (HAL_CAN_GetState(&hcan) == HAL_CAN_STATE_LISTENING)
    {
        return BOOT_CAN_STATUS_OK;
    }
    return BOOT_CAN_STATUS_ERROR;
}

bool boot_can_tx_idle(void)
{
    return HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 3U;
}
