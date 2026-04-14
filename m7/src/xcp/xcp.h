/***************************************************************
**
** NCU Header File
**
** File         :  xcp.h
** Module       :  xcp
** Author       :  SH
** Created      :  2026-04-14 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU XCP Interface
**
***************************************************************/

#ifndef XCP_H
#define XCP_H

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include <stdint.h>
#include <sys/sys.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define XCP_MAX_FRAME_SIZE (8) /* Max data length in XCP packet INCLUDING command */
#define XCP_IP_HEADER_SIZE (4)

/* 
** XCP COMMANDS (Client to Server)
*/

#define XCP_COMMAND_CONNECT         (0xFF)
#define XCP_COMMAND_DISCONNECT      (0xFE)
#define XCP_COMMAND_GET_STATUS      (0xFD)
#define XCP_COMMAND_SYNCH           (0xFC)
#define XCP_COMMAND_GET_COMM_MODE_INFO (0xFB)
#define XCP_COMMAND_GET_ID          (0xFA)s
#define XCP_COMMAND_SET_REQUEST     (0xF9)
#define XCP_COMMAND_GET_SEED        (0xF8)
#define XCP_COMMAND_UNLOCK          (0xF7)
#define XCP_COMMAND_SET_MTA         (0xF6)
#define XCP_COMMAND_UPLOAD          (0xF5)
#define XCP_COMMAND_SHORT_UPLOAD    (0xF4)
#define XCP_COMMAND_BUILD_CHECKSUM  (0xF3)
#define XCP_COMMAND_TRANSPORT_LAYER_CMD (0xF2)
#define XCP_COMMAND_USER_CMD        (0xF1)

#define XCP_COMMAND_DOWNLOAD        (0xF0) 
#define XCP_COMMAND_DOWNLOAD_NEXT   (0xEF)
#define XCP_COMMAND_DOWNLOAD_MAX    (0xEE)  
#define XCP_COMMAND_SHORT_DOWNLOAD  (0xED) 
#define XCP_COMMAND_MODIFY_BITS     (0xEC) 

#define XCP_COMMAND_GET_VERSION     (0xC0)
#define XCP_COMMAND_GET_VERSION_ALT (0x00)

/* 
** XCP RESPONSES (Server to Client)
*/

#define XCP_RESPONSE_OK             (0xFF)
#define XCP_RESPONSE_ERROR          (0xFE)
#define XCP_RESPONSE_EV             (0xFD)
#define XCP_RESPONSE_SERV           (0xFC)

/* CONNECT BITS */

#define XCP_CONN_RESP_RESOURCE_CAL_BIT  (0x00)
#define XCP_CONN_RESP_RESOURCE_DAQ_BIT  (0x02)
#define XCP_CONN_RESP_RESOURCE_STIM_BIT (0x03)
#define XCP_CONN_RESP_RESOURCE_PGM_BIT  (0x04)

/* 
** XCP ERROR CODES
*/

#define XCP_ERR_CMD_BUSY            (0x10)
#define XCP_ERR_DAQ_ACTIVE          (0x11)
#define XCP_ERR_PGM_ACTIVE          (0x12)
#define XCP_ERR_CMD_UNKNOWN         (0x20)
#define XCP_ERR_CMD_SYNTAX          (0x21)
#define XCP_ERR_OUT_OF_RANGE        (0x22)
#define XCP_ERR_WRITE_PROTECTED     (0x23)
#define XCP_ERR_ACCESS_DENIED       (0x24)
#define XCP_ERR_ACCESS_LOCKED       (0x25)
#define XCP_ERR_PAGE_NOT_VALID      (0x26)
#define XCP_ERR_MODE_NOT_VALID      (0x27)
#define XCP_ERR_SEGMENT_NOT_VALID   (0x28)
#define XCP_ERR_SEQUENCE            (0x29)
#define XCP_ERR_DAQ_CONFIG          (0x2A)
#define XCP_ERR_MEMORY_OVERFLOW     (0x30)
#define XCP_ERR_GENERIC             (0x31)
#define XCP_ERR_VERIFY              (0x32)
#define XCP_ERR_CMD_SYNCH           (0xFE)



/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef enum 
{
    XCP_CONN_TYPE_CAN,
    XCP_CONN_TYPE_ETH_UDP,
    XCP_CONN_TYPE_ETH_WEB_SOCKET,
    XCP_CONN_TYPE_MAX
} xcp_conn_type_t;

typedef union
{
    struct
    {
        uint32_t remote_ip;
        uint16_t remote_port;
        uint16_t counter;
    } ip;

    struct
    {
        uint32_t cto_id;
        uint32_t dto_id;
        uint8_t bus;
    } can;

    uint32_t raw[3];

} xcp_conn_info_t;

typedef struct
{
    uint8_t length; /* Length of command + data */
    uint8_t pid;
    uint8_t data[XCP_MAX_FRAME_SIZE - 1];
} xcp_frame_t;

typedef void (*xcp_response_handler_t)(xcp_conn_info_t *conn_info, xcp_frame_t *response_frame);

/***************************************************************
** MARK: FUNCTION DEFS
***************************************************************/

void xcp_init(void);

void xcp_add_response_handler(xcp_conn_type_t conn_type, xcp_response_handler_t handler);

/* Process a frame */
void xcp_receive_frame(xcp_conn_type_t conn_type, xcp_conn_info_t *conn_info, xcp_frame_t *frame);

void xcp_update(void);

#endif /* XCP_H */
