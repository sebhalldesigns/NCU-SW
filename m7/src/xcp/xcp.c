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
#include <icc.h>
#include <stdint.h>
#include <string.h>

#include <eth/eth.h>

/***************************************************************
** MARK: CONSTANTS & MACROS
***************************************************************/

#define XCP_MAX_SESSIONS (4)
#define XCP_TIMEOUT_US (500000000) /* 5 second timeout */
#define XCP_MAX_DAQ_LISTS_PER_SESSION   (4)
#define XCP_MAX_ODTS_PER_DAQ_LIST       (4)
#define XCP_MAX_ODT_ENTRIES_PER_ODT     (8)
#define XCP_CORE_VERBOSE_LOG (0U)

#if XCP_CORE_VERBOSE_LOG
#define XCP_CORE_LOG(msg) eth_log(msg)
#define XCP_CORE_LOG_U32(msg, val) eth_log_u32((msg), (val))
#else
#define XCP_CORE_LOG(msg) ((void)0)
#define XCP_CORE_LOG_U32(msg, val) ((void)0)
#endif

/***************************************************************
** MARK: TYPEDEFS
***************************************************************/

typedef enum
{
    XCP_SESSION_NONE,
    XCP_SESSION_DISCONNECTED,
    XCP_SESSION_CONNECTED,
    XCP_SESSION_DAQ_RUNNING
} xcp_session_state_t;

typedef struct
{
    uint8_t bit_offset;
    uint8_t size;
    uint8_t address_extension;
    uint32_t address;
} xcp_odt_entry_t;

typedef struct
{
    uint32_t num_entries;
    xcp_odt_entry_t entries[XCP_MAX_ODT_ENTRIES_PER_ODT];
} xcp_odt_t;

typedef struct
{
    uint32_t num_odts;
    xcp_odt_t odts[XCP_MAX_ODTS_PER_DAQ_LIST];

    uint8_t mode;
    uint16_t event_channel_num;
    uint8_t prescaler;
    uint8_t priority;

    uint32_t last_transmit_us;

    uint8_t start_stop; /* 0=stopped, 1=started */
} xcp_daq_list_t;

typedef struct
{
    bool active;
    uint32_t queued_at_us;
    uint8_t upload_size;
    uint8_t mta_extension;
    uint16_t reserved;
    uint32_t mta;
} deferred_request_t;

typedef struct
{
    xcp_session_state_t state;
    xcp_conn_type_t conn_type;
    xcp_conn_info_t conn_info;
    uint32_t frame_us;
    xcp_frame_t frame;
    bool new_frame;

    uint32_t mta; /* Master Target Address, used for memory access commands */
    uint8_t mta_extension;
    uint8_t upload_size;
    uint8_t download_size;

    bool block_upload_in_progress; /* unused for now */
    bool block_download_in_progress; /* unused for now */

    deferred_request_t deferred_upload;

    xcp_daq_list_t daq_lists[XCP_MAX_DAQ_LISTS_PER_SESSION];
    uint32_t daq_lists_in_use;

    /* as assigned by SET_DAQ_PTR */
    uint8_t active_daq_list; 
    uint8_t active_odt;
    uint8_t active_odt_entry;

    uint8_t daq_mode;

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
static void pack_get_status_response(xcp_frame_t *response);
static void pack_generic_response(xcp_frame_t *response); /* generic OK response */
static void pack_start_stop_daq_list_response(xcp_frame_t *response, uint8_t first_pid);
static void pack_error_response(xcp_frame_t *response, uint8_t error_code);

static bool service_deferred_upload(xcp_session_t *session, uint32_t now_us);
static bool is_m4_step_active(void);
static bool is_readable_region(uint32_t address, uint8_t size);
static bool translate_m4_alias_address(uint32_t address, uint32_t *translated);
static uint32_t normalize_m4_mailbox_address(uint32_t address);
static bool pack_upload_response(const deferred_request_t *request, xcp_frame_t *response);

/***************************************************************
** MARK: GLOBAL FUNCTIONS
***************************************************************/

void xcp_init()
{
    volatile icc_cal_mailbox_t *cal_mailbox = ICC_CAL_MAILBOX;

    session_count = 0;

    RCC->AHB4ENR |= RCC_AHB4ENR_HSEMEN;
    (void)RCC->AHB4ENR;

    cal_mailbox->pending = 0U;
    cal_mailbox->address = 0U;
    cal_mailbox->size = 0U;

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
                case XCP_CONN_TYPE_ETH_TCP:
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
            sessions[i].frame_us = sys_micros();
            memcpy(&sessions[i].frame, frame, sizeof(xcp_frame_t));
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

        sessions[session_count].frame_us = sys_micros();
        memcpy(&sessions[session_count].frame, frame, sizeof(xcp_frame_t));
        sessions[session_count].new_frame = true;

        session_count++;
    }
}

void xcp_disconnect_connection(xcp_conn_type_t conn_type, xcp_conn_info_t *conn_info)
{
    if (conn_info == NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < session_count; i++)
    {
        bool match = false;

        if (sessions[i].conn_type != conn_type)
        {
            continue;
        }

        switch (conn_type)
        {
            case XCP_CONN_TYPE_ETH_UDP:
            case XCP_CONN_TYPE_ETH_TCP:
                match = (sessions[i].conn_info.ip.remote_ip == conn_info->ip.remote_ip) &&
                        (sessions[i].conn_info.ip.remote_port == conn_info->ip.remote_port);
                break;

            case XCP_CONN_TYPE_CAN:
                match = (sessions[i].conn_info.can.cto_id == conn_info->can.cto_id) &&
                        (sessions[i].conn_info.can.dto_id == conn_info->can.dto_id) &&
                        (sessions[i].conn_info.can.bus == conn_info->can.bus);
                break;

            case XCP_CONN_TYPE_ETH_WEB_SOCKET:
                match = true;
                break;

            default:
                break;
        }

        if (match)
        {
            sessions[i].state = XCP_SESSION_DISCONNECTED;
            return;
        }
    }
}

void xcp_update()
{
    uint32_t now_us = sys_micros();

    for (uint32_t i = 0; i < session_count; i++)
    {
        if ((now_us - sessions[i].frame_us > XCP_TIMEOUT_US) || (sessions[i].state == XCP_SESSION_DISCONNECTED))
        {
            XCP_CORE_LOG("XCP client timed out or disconnected, closing connection");
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

        (void)service_deferred_upload(&sessions[i], now_us);


        if (sessions[i].state == XCP_SESSION_DAQ_RUNNING)
        {
            for (uint32_t daq_list_num = 0; daq_list_num < sessions[i].daq_lists_in_use; daq_list_num++)
            {
                xcp_daq_list_t *daq_list = &sessions[i].daq_lists[daq_list_num];

                if (daq_list->start_stop == 2 && (now_us - daq_list->last_transmit_us >= (uint32_t)daq_list->prescaler * 1000U * 100))
                {
                    xcp_frame_t daq_frame;
                    daq_frame.length = 2; /* PID only for now, expand as needed */
                    daq_frame.pid = 0x00; /* Placeholder PID, set as needed */
                    daq_frame.data[0] = 0x00; /* Placeholder data, set as needed */

                    daq_list->last_transmit_us = now_us;

                    response_handlers[sessions[i].conn_type](&sessions[i].conn_info, &daq_frame);

                }
            }

            

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
    bool send_response = true;

    switch (session->state)
    {
        case XCP_SESSION_NONE:
        {
            if (session->frame.pid == XCP_COMMAND_CONNECT)
            {
                session->state = XCP_SESSION_CONNECTED;
                XCP_CORE_LOG("XCP client connected");

                pack_conn_response(&response_frame);
                handled = true;
            }

        } break;

        case XCP_SESSION_CONNECTED:
        {
            switch (session->frame.pid)
            {
                case XCP_COMMAND_GET_STATUS:
                {
                    pack_get_status_response(&response_frame);
                    handled = true;
                } break;

                case XCP_COMMAND_SYNCH:
                {
                    pack_error_response(&response_frame, XCP_ERR_CMD_SYNCH);
                    handled = true;
                } break;

                case XCP_COMMAND_SET_MTA:
                {
                    /* data 0-1 reserved */

                    session->mta_extension = session->frame.data[2];

                    session->mta = 0;
                    session->mta |= session->frame.data[3];
                    session->mta |= ((uint32_t)session->frame.data[4]) << 8;
                    session->mta |= ((uint32_t)session->frame.data[5]) << 16;
                    session->mta |= ((uint32_t)session->frame.data[6]) << 24;

                    pack_generic_response(&response_frame);
                    handled = true;

                } break;

                case XCP_COMMAND_UPLOAD:
                {
                    if (session->deferred_upload.active)
                    {
                        pack_error_response(&response_frame, XCP_ERR_CMD_BUSY);
                        handled = true;
                    }
                    else
                    {
                        session->upload_size = session->frame.data[0];
                        if (session->upload_size == 0U || session->upload_size > (XCP_MAX_FRAME_SIZE - 1U))
                        {
                            pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                            handled = true;
                        }
                        else
                        {
                            session->deferred_upload.active = true;
                            session->deferred_upload.queued_at_us = sys_micros();
                            session->deferred_upload.upload_size = session->upload_size;
                            session->deferred_upload.mta_extension = session->mta_extension;
                            session->deferred_upload.mta = session->mta;

                            handled = true;
                            send_response = false;
                        }
                    }
                } break;

                case XCP_COMMAND_SHORT_UPLOAD:
                {
                    if (session->deferred_upload.active)
                    {
                        pack_error_response(&response_frame, XCP_ERR_CMD_BUSY);
                        handled = true;
                    }
                    else
                    {
                        session->upload_size = session->frame.data[0];

                        /* data[1] reserved */
                        
                        session->mta_extension = session->frame.data[2];

                        session->mta = 0;
                        session->mta |= session->frame.data[3];
                        session->mta |= ((uint32_t)session->frame.data[4]) << 8;
                        session->mta |= ((uint32_t)session->frame.data[5]) << 16;
                        session->mta |= ((uint32_t)session->frame.data[6]) << 24;

                        if (session->upload_size == 0U || session->upload_size > (XCP_MAX_FRAME_SIZE - 1U))
                        {
                            pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                            handled = true;
                        }
                        else
                        {
                            session->deferred_upload.active = true;
                            session->deferred_upload.queued_at_us = sys_micros();
                            session->deferred_upload.upload_size = session->upload_size;
                            session->deferred_upload.mta_extension = session->mta_extension;
                            session->deferred_upload.mta = session->mta;

                            handled = true;
                            send_response = false;
                        }
                    }

                } break;

                case XCP_COMMAND_DOWNLOAD:
                {
                    volatile icc_cal_mailbox_t *cal_mailbox = ICC_CAL_MAILBOX;
                    uint8_t write_size;
                    uint32_t write_address;
                    uint8_t i;

                    session->download_size = session->frame.data[0];
                    write_size = session->download_size;

#if XCP_CORE_VERBOSE_LOG
                    uint32_t data = 0x0;
                    if (write_size <= 4U)
                    {
                        for (i = 0U; i < write_size; i++)
                        {
                            data |= ((uint32_t)session->frame.data[1 + i]) << (i * 8);
                        }
                    }

                    XCP_CORE_LOG_U32("XCP DOWNLOAD", data);
#endif

                    if (write_size > ICC_CAL_MAILBOX_DATA_MAX) {
                        write_size = ICC_CAL_MAILBOX_DATA_MAX;
                    }

                    write_address = normalize_m4_mailbox_address(session->mta);

                    cal_mailbox->address = write_address;
                    cal_mailbox->size = write_size;
                    for (i = 0U; i < write_size; i++) {
                        cal_mailbox->data[i] = session->frame.data[1U + i];
                    }
                    __DMB();
                    cal_mailbox->pending = 1U;

                    session->mta += write_size;

                    pack_generic_response(&response_frame);
                    handled = true;
                } break;


                case XCP_COMMAND_FREE_DAQ:
                {
                    session->daq_lists_in_use = 0;

                    /* actually clear entries */
                    memset(session->daq_lists, 0, sizeof(session->daq_lists));

                    pack_generic_response(&response_frame);
                    handled = true;
                } break;

                case XCP_COMMAND_ALLOC_DAQ:
                {
                    uint16_t requested_lists = session->frame.data[2] << 8 | session->frame.data[1];
                    
                    if (requested_lists > XCP_MAX_DAQ_LISTS_PER_SESSION)
                    {
                        pack_error_response(&response_frame, XCP_ERR_MEMORY_OVERFLOW);
                    }
                    else
                    {
                        session->daq_lists_in_use = requested_lists;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;

                } break;

                case XCP_COMMAND_ALLOC_ODT:
                {
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    uint8_t odt_count = session->frame.data[3];

                    if (daq_list_num >= session->daq_lists_in_use || odt_count > XCP_MAX_ODTS_PER_DAQ_LIST)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->daq_lists[daq_list_num].num_odts = odt_count;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;
                    
                } break;

                case XCP_COMMAND_ALLOC_ODT_ENTRY:
                {
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    uint8_t odt_num = session->frame.data[3];
                    uint8_t entry_count = session->frame.data[4];

                    if (daq_list_num >= session->daq_lists_in_use || 
                        odt_num >= session->daq_lists[daq_list_num].num_odts || 
                        entry_count > XCP_MAX_ODT_ENTRIES_PER_ODT)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->daq_lists[daq_list_num].odts[odt_num].num_entries = entry_count;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;
                } break;

                case XCP_COMMAND_SET_DAQ_PTR:
                {
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    uint8_t odt_num = session->frame.data[3];
                    uint8_t odt_entry_num = session->frame.data[4];

                    if (daq_list_num >= session->daq_lists_in_use || 
                        odt_num >= session->daq_lists[daq_list_num].num_odts || 
                        odt_entry_num >= session->daq_lists[daq_list_num].odts[odt_num].num_entries)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->active_daq_list = daq_list_num;
                        session->active_odt = odt_num;
                        session->active_odt_entry = odt_entry_num;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;

                } break;

                case XCP_COMMAND_WRITE_DAQ:
                {
                    if (session->active_daq_list >= session->daq_lists_in_use || 
                        session->active_odt >= session->daq_lists[session->active_daq_list].num_odts || 
                        session->active_odt_entry >= session->daq_lists[session->active_daq_list].odts[session->active_odt].num_entries)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        xcp_odt_entry_t *entry = &session->daq_lists[session->active_daq_list].odts[session->active_odt].entries[session->active_odt_entry];
                        
                        entry->bit_offset = session->frame.data[0];
                        entry->size = session->frame.data[1];
                        entry->address_extension = session->frame.data[2];

                        entry->address = 0;
                        entry->address |= session->frame.data[3];
                        entry->address |= ((uint32_t)session->frame.data[4]) << 8;
                        entry->address |= ((uint32_t)session->frame.data[5]) << 16;
                        entry->address |= ((uint32_t)session->frame.data[6]) << 24;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;
                } break;

                case XCP_COMMAND_SET_DAQ_LIST_MODE:
                {
                    uint8_t mode = session->frame.data[0];
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    uint16_t event_channel_num = session->frame.data[4] << 8 | session->frame.data[3];
                    uint8_t prescaler = session->frame.data[5];
                    uint8_t priority = session->frame.data[6];

                    if (daq_list_num >= session->daq_lists_in_use)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->daq_lists[daq_list_num].mode = mode;
                        session->daq_lists[daq_list_num].event_channel_num = event_channel_num;
                        session->daq_lists[daq_list_num].prescaler = prescaler;
                        session->daq_lists[daq_list_num].priority = priority;

                        pack_generic_response(&response_frame);
                    }

                    handled = true;
                } break;

                case XCP_COMMAND_START_STOP_DAQ_LIST:
                {
                    uint8_t start_stop = session->frame.data[0];
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    
                    if (daq_list_num >= session->daq_lists_in_use)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->daq_lists[daq_list_num].start_stop = start_stop;

                        /* MATLAB expects START_STOP_DAQ_LIST response to include first PID. */
                        pack_start_stop_daq_list_response(&response_frame, 0x00);
                    }

                    handled = true;
                } break;

                case XCP_COMMAND_START_STOP_SYNCH:
                {
                    uint8_t start_stop = session->frame.data[0];
                    
                    session->daq_mode = start_stop;

                    if (start_stop == 1)
                    {
                        session->state = XCP_SESSION_DAQ_RUNNING;
                        XCP_CORE_LOG("DAQ started");
                    }

                    pack_generic_response(&response_frame);

                    handled = true;
                } break;

                default:
                {
                    /* do nothing */
                } break;
            }
        } break;

        case XCP_SESSION_DAQ_RUNNING:
        {
            switch (session->frame.pid)
            {
                case XCP_COMMAND_START_STOP_DAQ_LIST:
                {
                    uint8_t start_stop = session->frame.data[0];
                    uint16_t daq_list_num = session->frame.data[2] << 8 | session->frame.data[1];
                    
                    if (daq_list_num >= session->daq_lists_in_use)
                    {
                        pack_error_response(&response_frame, XCP_ERR_OUT_OF_RANGE);
                    }
                    else
                    {
                        session->daq_lists[daq_list_num].start_stop = start_stop;

                        /* MATLAB expects START_STOP_DAQ_LIST response to include first PID. */
                        pack_start_stop_daq_list_response(&response_frame, 0x00);
                    }

                    handled = true;
                } break;

                case XCP_COMMAND_START_STOP_SYNCH:
                {
                    uint8_t start_stop = session->frame.data[0];
                    
                    session->daq_mode = start_stop;

                    if (start_stop == 1)
                    {
                        session->state = XCP_SESSION_DAQ_RUNNING;
                        XCP_CORE_LOG("DAQ started");
                    }

                    pack_generic_response(&response_frame);

                    handled = true;
                } break;

                default:
                {

                } break;
            }
            
        } break;

        default:
        {
            /* do nothing */
        } break;
    }

    /* always allow disconnection */
    if (session->frame.pid == XCP_COMMAND_DISCONNECT)
    {
        session->state = XCP_SESSION_DISCONNECTED;
        session->deferred_upload.active = false;
        XCP_CORE_LOG("XCP client requested disconnect");

        pack_generic_response(&response_frame);
        handled = true;
    }

    if (!handled)
    {
        /* if we get here, it's an unsupported message so error */
        XCP_CORE_LOG_U32("Invalid XCP PID", session->frame.pid);
        XCP_CORE_LOG_U32("For state", session->state);

        pack_error_response(&response_frame, XCP_ERR_CMD_UNKNOWN); /* Generic error code, expand as needed */

    }

    if (send_response)
    {
        response_handlers[session->conn_type](&session->conn_info, &response_frame);
    }
}

static void close_connection(xcp_session_t *session)
{
    if (session == NULL)
    {
        return;
    }

    memset(session, 0, sizeof(*session));
}

static bool service_deferred_upload(xcp_session_t *session, uint32_t now_us)
{
    xcp_frame_t response_frame;

    if ((session == NULL) || !session->deferred_upload.active)
    {
        return false;
    }

    if (is_m4_step_active())
    {
        if ((now_us - session->deferred_upload.queued_at_us) < ICC_XCP_UPLOAD_TIMEOUT_US)
        {
            return false;
        }

        pack_error_response(&response_frame, XCP_ERR_CMD_BUSY);
        response_handlers[session->conn_type](&session->conn_info, &response_frame);
        session->deferred_upload.active = false;
        return true;
    }

    if (!pack_upload_response(&session->deferred_upload, &response_frame))
    {
        pack_error_response(&response_frame, XCP_ERR_ACCESS_DENIED);
    }
    else
    {
        session->mta = session->deferred_upload.mta + session->deferred_upload.upload_size;
    }

    response_handlers[session->conn_type](&session->conn_info, &response_frame);
    session->deferred_upload.active = false;

    return true;
}

static bool is_m4_step_active(void)
{
    return (HSEM->R[ICC_M4_ACTIVITY_HSEM_ID] & HSEM_R_LOCK) != 0U;
}

static bool is_readable_region(uint32_t address, uint8_t size)
{
    uint32_t end_address;
    uint32_t translated_start = address;
    uint32_t translated_end;
    uint32_t sram_limit = ICC_M4_SRAM_BUS_BASE + ICC_M4_SRAM_SIZE_BYTES;
    uint32_t d3_limit = ICC_D3_SRAM_BASE + ICC_D3_SRAM_SIZE_BYTES;
    uint32_t flash_limit = ICC_M4_FLASH_BASE + ICC_M4_FLASH_SIZE_BYTES;

    if (size == 0U)
    {
        return false;
    }

    end_address = address + ((uint32_t)size - 1U);
    if (end_address < address)
    {
        return false;
    }

    if (!translate_m4_alias_address(address, &translated_start) ||
        !translate_m4_alias_address(end_address, &translated_end))
    {
        return false;
    }

    if (translated_end < translated_start)
    {
        return false;
    }

    if (translated_start >= ICC_M4_SRAM_BUS_BASE && translated_end < sram_limit)
    {
        return true;
    }

    if (translated_start >= ICC_D3_SRAM_BASE && translated_end < d3_limit)
    {
        return true;
    }

    if (translated_start >= ICC_M4_FLASH_BASE && translated_end < flash_limit)
    {
        return true;
    }

    return false;
}

static bool translate_m4_alias_address(uint32_t address, uint32_t *translated)
{
    uint32_t alias_limit = ICC_M4_SRAM_ALIAS_BASE + ICC_M4_SRAM_SIZE_BYTES;

    if (translated == NULL)
    {
        return false;
    }

    if (address >= ICC_M4_SRAM_ALIAS_BASE && address < alias_limit)
    {
        *translated = address - ICC_M4_SRAM_ALIAS_BASE + ICC_M4_SRAM_BUS_BASE;
        return true;
    }

    *translated = address;
    return true;
}

static uint32_t normalize_m4_mailbox_address(uint32_t address)
{
    uint32_t bus_limit = ICC_M4_SRAM_BUS_BASE + ICC_M4_SRAM_SIZE_BYTES;

    if (address >= ICC_M4_SRAM_BUS_BASE && address < bus_limit) {
        return address - ICC_M4_SRAM_BUS_BASE + ICC_M4_SRAM_ALIAS_BASE;
    }

    return address;
}

static void pack_conn_response(xcp_frame_t *response)
{
    response->length = 8;
    response->pid = XCP_RESPONSE_OK;
    response->data[0] = 0x00; /* RESOURCE */
    response->data[0] |= 1 << XCP_CONN_RESP_RESOURCE_CAL_BIT; /* CAL resource available */
    response->data[0] |= 1 << XCP_CONN_RESP_RESOURCE_DAQ_BIT; /* DAQ resource NOT available */
    response->data[0] |= 0 << XCP_CONN_RESP_RESOURCE_STIM_BIT; /* STIM resource NOT available */
    response->data[0] |= 0 << XCP_CONN_RESP_RESOURCE_PGM_BIT; /* PGM resource NOT available */

    response->data[1] = 0x80; /* COMM_MODE_BASIC, Little Endian */
    response->data[2] = XCP_MAX_FRAME_SIZE; /* MAX_CTO */
    response->data[3] = XCP_MAX_FRAME_SIZE; /* MAX_DTO low byte */
    response->data[4] = 0x00;               /* MAX_DTO high byte */
    response->data[5] = 0x01; /* XCP PROTOCOL VERSION */
    response->data[6] = 0x01; /* XCP TRANSPORT VERSION */
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

static void pack_generic_response(xcp_frame_t *response)
{
    response->length = 1;
    response->pid = XCP_RESPONSE_OK;
}

static void pack_start_stop_daq_list_response(xcp_frame_t *response, uint8_t first_pid)
{
    response->length = 2;
    response->pid = XCP_RESPONSE_OK;
    response->data[0] = first_pid;
}

static bool pack_upload_response(const deferred_request_t *request, xcp_frame_t *response)
{
    uint32_t translated_mta;

    if ((request == NULL) || (response == NULL) ||
        !is_readable_region(request->mta, request->upload_size) ||
        !translate_m4_alias_address(request->mta, &translated_mta))
    {
        return false;
    }

    response->length = 1U + request->upload_size;
    response->pid = XCP_RESPONSE_OK;
    for (uint8_t i = 0; i < request->upload_size; i++)
    {
        response->data[i] = *((volatile uint8_t *)(translated_mta + i));
    }

    return true;
}
