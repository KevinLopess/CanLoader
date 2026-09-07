#include "boot_can.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_j1939.h"
#include "main.h"

#define J1939_DA_ID_MASK (UINT32_C(0xFF) << 8U)
#define J1939_PGN_ID_MASK (UINT32_C(0x3FFFF) << 8U)
#define FDCAN_TX_BUFFER_MASK \
    (FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2)

static bool bus_is_off(void)
{
    FDCAN_ProtocolStatusTypeDef status = {0};

    return (HAL_FDCAN_GetProtocolStatus(&hfdcan1, &status) != HAL_OK) ||
           (status.BusOff != 0U);
}

static bool configure_destination_filter(uint32_t index, uint8_t address)
{
    FDCAN_FilterTypeDef filter = {0};

    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = index;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = (uint32_t)address << 8U;
    filter.FilterID2 = J1939_DA_ID_MASK;
    return HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) == HAL_OK;
}

static bool configure_pgn_filter(uint32_t index, uint32_t pgn)
{
    FDCAN_FilterTypeDef filter = {0};

    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = index;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = pgn << 8U;
    filter.FilterID2 = J1939_PGN_ID_MASK;
    return HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) == HAL_OK;
}

bool boot_can_configure_address(uint8_t local_address)
{
    if (local_address > BOOT_J1939_NULL_ADDRESS)
    {
        return false;
    }

    return configure_destination_filter(0U, local_address) &&
           configure_destination_filter(1U, BOOT_J1939_GLOBAL_ADDRESS) &&
           configure_pgn_filter(2U, BOOT_J1939_PGN_COMMANDED_ADDRESS);
}

bool boot_can_init(uint8_t local_address)
{
    return boot_can_configure_address(local_address) &&
           (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                         FDCAN_REJECT,
                                         FDCAN_REJECT,
                                         FDCAN_REJECT_REMOTE,
                                         FDCAN_REJECT_REMOTE) == HAL_OK);
}

bool boot_can_abort_pending_tx(void)
{
    uint32_t timeout = UINT32_C(100000);

    if (HAL_FDCAN_AbortTxRequest(&hfdcan1, FDCAN_TX_BUFFER_MASK) != HAL_OK)
    {
        return false;
    }
    while ((HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) != 3U) && (timeout > 0U))
    {
        --timeout;
    }
    return timeout != 0U;
}

bool boot_can_start(void)
{
    return HAL_FDCAN_Start(&hfdcan1) == HAL_OK;
}

void boot_can_stop(void)
{
    if (HAL_FDCAN_GetState(&hfdcan1) == HAL_FDCAN_STATE_BUSY)
    {
        (void)HAL_FDCAN_Stop(&hfdcan1);
    }
    (void)HAL_FDCAN_DeInit(&hfdcan1);
}

boot_can_status_t boot_can_send(const boot_can_frame_t *frame)
{
    FDCAN_TxHeaderTypeDef header = {0};

    if ((frame == NULL) || (frame->id_type != BOOT_CAN_ID_EXTENDED) ||
        (frame->id > BOOT_CAN_EXTENDED_ID_MAX) ||
        (frame->dlc > BOOT_CAN_MAX_DATA_LENGTH))
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    if (bus_is_off())
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U)
    {
        return BOOT_CAN_STATUS_BUSY;
    }

    header.Identifier = frame->id;
    header.IdType = FDCAN_EXTENDED_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = frame->dlc;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_OFF;
    header.FDFormat = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1,
                                      &header,
                                      (uint8_t *)frame->data) != HAL_OK)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_receive(boot_can_frame_t *frame)
{
    FDCAN_RxHeaderTypeDef header = {0};
    uint8_t index;

    if (frame == NULL)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    if (bus_is_off())
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) == 0U)
    {
        return BOOT_CAN_STATUS_EMPTY;
    }
    if (HAL_FDCAN_GetRxMessage(&hfdcan1,
                               FDCAN_RX_FIFO0,
                               &header,
                               frame->data) != HAL_OK)
    {
        return BOOT_CAN_STATUS_ERROR;
    }
    if ((header.IdType != FDCAN_EXTENDED_ID) ||
        (header.RxFrameType != FDCAN_DATA_FRAME) ||
        (header.FDFormat != FDCAN_CLASSIC_CAN) ||
        (header.Identifier > BOOT_CAN_EXTENDED_ID_MAX) ||
        (header.DataLength > FDCAN_DLC_BYTES_8))
    {
        return BOOT_CAN_STATUS_ERROR;
    }

    frame->id = header.Identifier;
    frame->id_type = BOOT_CAN_ID_EXTENDED;
    frame->dlc = (uint8_t)header.DataLength;
    for (index = frame->dlc; index < BOOT_CAN_MAX_DATA_LENGTH; ++index)
    {
        frame->data[index] = UINT8_C(0xFF);
    }
    return BOOT_CAN_STATUS_OK;
}

boot_can_status_t boot_can_get_state(void)
{
    if (bus_is_off())
    {
        return BOOT_CAN_STATUS_BUS_OFF;
    }
    return (HAL_FDCAN_GetState(&hfdcan1) == HAL_FDCAN_STATE_BUSY) ?
           BOOT_CAN_STATUS_OK : BOOT_CAN_STATUS_ERROR;
}

bool boot_can_tx_idle(void)
{
    return HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 3U;
}
