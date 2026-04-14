/***************************************************************
**
** NCU Source File
**
** File         :  xcp.c
** Module       :  xcp
** Author       :  SH
** Created      :  2026-04-14 (YYYY-MM-DD)
** License      :  MIT
** Description  :  NCU XCP Interface
**
***************************************************************/

/***************************************************************
** MARK: INCLUDES
***************************************************************/

#include "xcp.h"
#include <string.h>

#include <eth/eth.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define XCP_MAX_SESSIONS (4)
#define XCP_TIMEOUT_US (500000000) /* 5 second timeout */

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef enum
{
    XCP_SESSION_NONE,
    XCP_SESSION_DISCONNECTED,
    XCP_SESSION_CONNECTED
} xcp_session_state_t;

typedef struct
{
    xcp_session_state_t state;
    xcp_conn_type_t conn_type;
    xcp_conn_info_t conn_info;
    uint32_t last_frame_us;
    xcp_frame_t last_frame;
    bool new_frame;
} xcp_session_t;

/***************************************************************
** MARK: STATIC VARIABLES
***************************************************************/

static xcp_session_t sessions[XCP_MAX_SESSIONS];
static uint32_t session_count = 0;

static xcp_response_handler_t response_handlers[XCP_CONN_TYPE_MAX];

/***************************************************************
** MARK: STATIC FUNCTION DEFS
***************************************************************/

static void process_new_frame(xcp_session_t *session);
static void close_connection(xcp_session_t *session);

static void pack_conn_response(xcp_frame_t *response);
static void pack_disconn_response(xcp_frame_t *response);
static void pack_get_status_response(xcp_frame_t *response);
static void pack_error_response(xcp_frame_t *response, uint8_t error_code);

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

void xcp_init()
{
    session_count = 0;

    for (uint32_t i = 0; i < XCP_CONN_TYPE_MAX; i++)
    {
        response_handlers[i] = NULL;
    }
}

void xcp_add_response_handler(xcp_conn_type_t conn_type, xcp_response_handler_t handler)
{
    if (conn_type < XCP_CONN_TYPE_MAX)
    {
        response_handlers[conn_type] = handler;
    }
}

void xcp_receive_frame(xcp_conn_type_t conn_type, xcp_conn_info_t *conn_info, xcp_frame_t *frame)
{
    if (response_handlers[conn_type] == NULL)
    {
        /* No handler for this connection type, ignore frame */
        return;
    }

    for (uint32_t i = 0; i < session_count; i++)
    {
        bool match = false;

        if (sessions[i].conn_type == conn_type)
        {
            switch (conn_type)
            {
                case XCP_CONN_TYPE_ETH_UDP:
                    match = (sessions[i].conn_info.ip.remote_ip == conn_info->ip.remote_ip) &&
                            (sessions[i].conn_info.ip.remote_port == conn_info->ip.remote_port);
                    break;

                case XCP_CONN_TYPE_CAN:
                    match = (sessions[i].conn_info.can.cto_id == conn_info->can.cto_id) &&
                            (sessions[i].conn_info.can.dto_id == conn_info->can.dto_id) &&
                            (sessions[i].conn_info.can.bus == conn_info->can.bus);
                    break;

                case XCP_CONN_TYPE_ETH_WEB_SOCKET:
                    match = true; /* Placeholder, implement as needed */
                    break;

                default:
                    break;
            }
        }

        if (match)
        {
            
            /* Update existing session */
            sessions[i].last_frame_us = sys_micros();
            memcpy(&sessions[i].last_frame, frame, sizeof(xcp_frame_t));
            sessions[i].new_frame = true;

            return;
        }
    }

    /* Create new session if possible */

    if (session_count < XCP_MAX_SESSIONS)
    {
        sessions[session_count].state = XCP_SESSION_NONE;
        sessions[session_count].conn_type = conn_type;
        memcpy(&sessions[session_count].conn_info, conn_info, sizeof(xcp_conn_info_t));

        sessions[session_count].last_frame_us = sys_micros();
        memcpy(&sessions[session_count].last_frame, frame, sizeof(xcp_frame_t));
        sessions[session_count].new_frame = true;

        session_count++;
    }
}

void xcp_update()
{
    uint32_t now_us = sys_micros();

    for (uint32_t i = 0; i < session_count; i++)
    {
        if ((now_us - sessions[i].last_frame_us > XCP_TIMEOUT_US) || (sessions[i].state == XCP_SESSION_DISCONNECTED))
        {
            eth_log("XCP client timed out or disconnected, closing connection");
            close_connection(&sessions[i]);

            for (uint32_t j = i; j < session_count - 1; j++)
            {
                sessions[j] = sessions[j + 1];
            }

            session_count--;
        }
        else if (sessions[i].new_frame)
        {
            sessions[i].new_frame = false;

            process_new_frame(&sessions[i]);
        }
    }
}

/***************************************************************
** MARK: STATIC FUNCTIONS
***************************************************************/

static void process_new_frame(xcp_session_t *session)
{
    static xcp_frame_t response_frame;

    bool handled = false;

    switch (session->state)
    {
        case XCP_SESSION_NONE:
        {
            if (session->last_frame.pid == XCP_COMMAND_CONNECT)
            {
                session->state = XCP_SESSION_CONNECTED;
                eth_log("XCP client connected");

                pack_conn_response(&response_frame);
                handled = true;
            }

        } break;

        case XCP_SESSION_CONNECTED:
        {
            switch (session->last_frame.pid)
            {
                case XCP_COMMAND_GET_STATUS:
                {
                    pack_get_status_response(&response_frame);
                    handled = true;
                } break;

                default:
                {
                    /* do nothing */
                } break;
            }
        } break;

        default:
        {
            /* do nothing */
        } break;
    }

    /* always allow disconnection */
    if (session->last_frame.pid == XCP_COMMAND_DISCONNECT)
    {
        session->state = XCP_SESSION_DISCONNECTED;
        eth_log("XCP client requested disconnect");

        pack_disconn_response(&response_frame);
        handled = true;
    }

    if (!handled)
    {
        /* if we get here, it's an unsupported message so error */
        eth_log_u32("Invalid XCP PID", session->last_frame.pid);
        eth_log_u32("For state", session->state);

        pack_error_response(&response_frame, 0x01); /* Generic error code, expand as needed */
    }

    response_handlers[session->conn_type](&session->conn_info, &response_frame);
}

static void close_connection(xcp_session_t *session)
{

    

}

static void pack_conn_response(xcp_frame_t *response)
{
    response->length = 8;
    response->pid = XCP_RESPONSE_OK;
    response->data[0] = 0x00; /* RESOURCE */
    response->data[1] = 0x80; /* COMM_MODE_BASIC, Little Endian */
    response->data[2] = XCP_MAX_FRAME_SIZE; /* MAX_CTO */
    response->data[3] = XCP_MAX_FRAME_SIZE; /* MAX_DTO low byte */
    response->data[4] = 0x00;               /* MAX_DTO high byte */
    response->data[5] = 0x01; /* XCP PROTOCOL VERSION */
    response->data[6] = 0x01; /* XCP TRANSPORT VERSION */
}

static void pack_disconn_response(xcp_frame_t *response)
{
    response->length = 2;
    response->pid = XCP_RESPONSE_OK;
    response->data[0] = 0x00; /* OK */
}

static void pack_get_status_response(xcp_frame_t *response)
{
    response->length = 6;
    response->pid = XCP_RESPONSE_OK;
    response->data[0] = 0x00; /* SESSION STATUS */
    response->data[1] = 0x00; /* PROTECTION STATUS */
    response->data[2] = 0x00; /* RESERVED */
    response->data[3] = 0x00; /* SESSION CONFIG ID low byte */
    response->data[4] = 0x00; /* SESSION CONFIG ID high byte */

}

static void pack_error_response(xcp_frame_t *response, uint8_t error_code)
{
    response->length = 2;
    response->pid = XCP_RESPONSE_ERROR;
    response->data[0] = error_code;
}
