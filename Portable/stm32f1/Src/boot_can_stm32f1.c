#include "boot_can.h"

#include <stddef.h>

#include "boot_config.h"
#include "main.h"

extern CAN_HandleTypeDef hcan;

static uint32_t filter_value(uint32_t standard_id)
{
    return standard_id << 5U;
}

bool boot_can_init(uint8_t node_id)
{
    CAN_FilterTypeDef filter = {0};

    if ((node_id < BOOT_MIN_NODE_ID) || (node_id > BOOT_MAX_NODE_ID))
    {
        return false;
    }

    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDLIST;
    filter.FilterScale = CAN_FILTERSCALE_16BIT;
    filter.FilterIdHigh = filter_value(BOOT_CAN_COMMAND_ID(node_id));
    filter.FilterIdLow = filter_value(BOOT_CAN_DATA_ID(node_id));
    filter.FilterMaskIdHigh = filter_value(BOOT_CAN_BROADCAST_ID);
    filter.FilterMaskIdLow = filter_value(BOOT_CAN_COMMAND_ID(node_id));
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;

    return HAL_CAN_ConfigFilter(&hcan, &filter) == HAL_OK;
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

    if ((frame == NULL) || (frame->id > UINT32_C(0x7FF)) ||
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

    header.StdId = frame->id;
    header.ExtId = 0U;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->dlc;
    header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&hcan, &header, (uint8_t *)frame->data, &mailbox) != HAL_OK)
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

    if ((header.IDE != CAN_ID_STD) || (header.RTR != CAN_RTR_DATA) ||
        (header.DLC > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return BOOT_CAN_STATUS_ERROR;
    }

    frame->id = header.StdId;
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
