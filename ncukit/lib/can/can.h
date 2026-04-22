#ifndef NCUKIT_LIB_CAN_H
#define NCUKIT_LIB_CAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef MATLAB_MEX_FILE
    /* Simulink run and check */
#else
    /* Embedded target build */
    #define STM32H745xx
    #include <stm32h7xx.h>
#endif

typedef unsigned int can_rx_count_t;

typedef enum {
    CAN_BUS_1 = 0U,
    CAN_BUS_2 = 1U
} can_bus_t;

typedef enum {
    CAN_BITRATE_125K = 0U,
    CAN_BITRATE_250K = 1U,
    CAN_BITRATE_500K = 2U,
    CAN_BITRATE_1M   = 3U
} can_bitrate_t;

typedef enum {
    CAN_RX_STATUS_EMPTY       = 0U,
    CAN_RX_STATUS_OK          = 1U,
    CAN_RX_STATUS_INVALID_BUS = 2U,
    CAN_RX_STATUS_NOT_READY   = 3U
} can_rx_status_t;

#ifndef DEFINED_TYPEDEF_FOR_CAN_MESSAGE_BUS_
#define DEFINED_TYPEDEF_FOR_CAN_MESSAGE_BUS_
typedef struct {
  uint8_t Extended;
  uint8_t Length;
  uint8_t Remote;
  uint8_t Error;
  uint32_t ID;
  double Timestamp;
  uint8_t Data[8];
} CAN_MESSAGE_BUS;
#endif

#ifndef CAN1_DEFAULT_BITRATE
#define CAN1_DEFAULT_BITRATE CAN_BITRATE_500K
#endif

#ifndef CAN2_DEFAULT_BITRATE
#define CAN2_DEFAULT_BITRATE CAN_BITRATE_500K
#endif

void can_set_bitrate(uint8_t bus, uint8_t bitrate);

void can_transmit(uint8_t bus, const CAN_MESSAGE_BUS *message);
CAN_MESSAGE_BUS can_receive(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count);
CAN_MESSAGE_BUS can_receive_message(uint8_t bus, uint8_t *status, can_rx_count_t *rx_count);
void can_receive_full(uint8_t bus, CAN_MESSAGE_BUS *message, uint8_t *status, can_rx_count_t *rx_count);

void can1_set_bitrate(can_bitrate_t bitrate);
void can2_set_bitrate(can_bitrate_t bitrate);

void can1_transmit(CAN_MESSAGE_BUS message);
void can2_transmit(CAN_MESSAGE_BUS message);

CAN_MESSAGE_BUS can1_receive(uint8_t *status, can_rx_count_t *rx_count);
CAN_MESSAGE_BUS can2_receive(uint8_t *status, can_rx_count_t *rx_count);

#endif /* NCUKIT_LIB_CAN_H */
