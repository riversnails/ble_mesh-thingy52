
/* Copyright (c) 2010 - 2019, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "test_model_server.h"
#include "generic_onoff_common.h"
#include "generic_onoff_messages.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "access.h"
#include "access_config.h"
#include "nrf_mesh_assert.h"
#include "nrf_mesh_utils.h"
#include "nordic_common.h"

#include "test_model.h"
#include "log.h"
#include "device_manager.h"


static uint32_t status_send(custom_server_t * p_server,
                            const access_message_rx_t * p_message,
                            const custom_status_params_t * p_params)
{
    custom_status_msg_pkt_t msg_pkt;

    msg_pkt.temp_intager = p_params->temp_intager;
    msg_pkt.temp_decimal = p_params->temp_decimal;
    msg_pkt.humi = p_params->humi;
    msg_pkt.tid = p_params->tid;
    msg_pkt.ttl = p_params->ttl;
    strcpy(msg_pkt.addr, p_params->addr);
    //msg_pkt.addr = p_params->addr;

    access_message_tx_t reply =
    {
        .opcode = ACCESS_OPCODE_SIG(CUSTOM_OPCODE_STATUS),
        .p_buffer = (const uint8_t *) &msg_pkt,
        .length = CUSTOM_SET_MAXLEN,
        .force_segmented = p_server->settings.force_segmented,
        .transmic_size = p_server->settings.transmic_size
    };

    if (p_message == NULL)
    {
        return access_model_publish(p_server->model_handle, &reply);
    }
    else
    {
        return access_model_reply(p_server->model_handle, p_message, &reply);
    }
}

static void periodic_publish_cb(access_model_handle_t handle, void * p_args)
{
    custom_server_t * p_server = (custom_server_t *)p_args;
    custom_status_params_t out_data = {0};

    p_server->settings.p_callbacks->onoff_cbs.get_cb(p_server, NULL, &out_data);
    (void) status_send(p_server, NULL, &out_data);
}

/** Opcode Handlers */

static inline bool set_params_validate(const access_message_rx_t * p_rx_msg, const custom_set_msg_pkt_t * p_params)
{
    return (
            (p_rx_msg->length == CUSTOM_SET_MINLEN || p_rx_msg->length == CUSTOM_SET_MAXLEN)
           );
}

static void handle_set(access_model_handle_t model_handle, const access_message_rx_t * p_rx_msg, void * p_args)
{
    custom_server_t * p_server = (custom_server_t *) p_args;
    custom_set_params_t in_data = {0};
    model_transition_t in_data_tr = {0};
    custom_status_params_t out_data = {0};
    custom_set_msg_pkt_t * p_msg_params_packed = (custom_set_msg_pkt_t *) p_rx_msg->p_data;

    if (true)
    {
        uint8_t num;
        in_data.temp_intager = p_msg_params_packed->temp_intager;
        in_data.temp_decimal = p_msg_params_packed->temp_decimal;
        in_data.humi = p_msg_params_packed->humi;
        in_data.ttl = p_msg_params_packed->ttl - 1;
        in_data.tid = p_msg_params_packed->tid;
        strcpy(in_data.addr, p_msg_params_packed->addr);


        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "addr = %d tids = %d %d\n", in_data.addr, get_tid(in_data.addr), (uint8_t)in_data.tid); 
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "tids = %d %d\n", get_tid(in_data.addr), (uint8_t)in_data.tid); 
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "tid = %d ttl = %d addr = %d\n",   
        //                                  in_data.tid, in_data.ttl, in_data.addr);

        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "t_data = %2d.%-2d h_data = %2d tid = %d ttl = %d\n", 
        //                                    in_data.temp_intager, in_data.temp_decimal, in_data.humi,
        //                                    in_data.tid, in_data.ttl);
        //print_addr(in_data.addr);

        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "tid : %d\n", in_data.tid);

        bool check_first = get_num(&num, in_data.addr);
        
        if(check_first == false && comp_addr(in_data.addr, get_this_addr()) == false)
        {
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "first_time!\n");
            //print_ttl(model_handle);
            print_addr(in_data.addr);
            
            if(check_requset_server(in_data.humi, in_data.temp_intager, in_data.temp_decimal))
            {
                if(packet_response(in_data) == true)
                {
                    set_inst_tid((uint8_t)in_data.tid, num);
                }
            }
        }
        
        if(check_requset_server(in_data.humi, in_data.temp_intager, in_data.temp_decimal) &&
            get_inst_tid(num) != (uint8_t)in_data.tid     )
        {
            if(packet_response(in_data) == true)
            {
                __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "inst in!\n");
                //print_ttl(model_handle);
                set_inst_tid((uint8_t)in_data.tid, num);
            }
        }
        else if (get_tid(num) != (uint8_t)in_data.tid               && 
                  get_inst_tid(num) != (uint8_t)in_data.tid         &&
                  comp_addr(in_data.addr, get_this_addr()) == false &&
                  get_available(num)   )
        {

            //print_ttl(model_handle);
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "t_data = %2d.%-2d h_data = %2d tid = %d ttl = %d\n", 
                                                in_data.temp_intager, in_data.temp_decimal, in_data.humi,
                                                in_data.tid, in_data.ttl);
            print_addr(in_data.addr);
            
            set_tid((uint8_t)in_data.tid, num);

            p_server->settings.p_callbacks->onoff_cbs.set_cb(p_server,
                                                            &p_rx_msg->meta_data,
                                                            &in_data,
                                                            (p_rx_msg->length == CUSTOM_SET_MINLEN) ? NULL : &in_data_tr,
                                                            (p_rx_msg->opcode.opcode == CUSTOM_OPCODE_SET) ? &out_data : NULL);

            if (p_rx_msg->opcode.opcode == CUSTOM_OPCODE_SET)
            {
                (void) status_send(p_server, p_rx_msg, &out_data);
            }
        }
    }
}

static inline bool get_params_validate(const access_message_rx_t * p_rx_msg)
{
    return (p_rx_msg->length == 0);
}

static void handle_get(access_model_handle_t model_handle, const access_message_rx_t * p_rx_msg, void * p_args)
{
    custom_server_t * p_server = (custom_server_t *) p_args;
    custom_status_params_t out_data = {0};

    if (get_params_validate(p_rx_msg))
    {
        p_server->settings.p_callbacks->onoff_cbs.get_cb(p_server, &p_rx_msg->meta_data, &out_data);
        (void) status_send(p_server, p_rx_msg, &out_data);
    }
}

static const access_opcode_handler_t m_opcode_handlers[] =
{
    {ACCESS_OPCODE_SIG(CUSTOM_OPCODE_SET), handle_set},
    {ACCESS_OPCODE_SIG(CUSTOM_OPCODE_SET_UNACKNOWLEDGED), handle_set},
    {ACCESS_OPCODE_SIG(CUSTOM_OPCODE_GET), handle_get},
};


/** Interface functions */
uint32_t custom_server_init(custom_server_t * p_server, uint8_t element_index)
{
    if (p_server == NULL ||
        p_server->settings.p_callbacks == NULL ||
        p_server->settings.p_callbacks->onoff_cbs.set_cb == NULL ||
        p_server->settings.p_callbacks->onoff_cbs.get_cb == NULL )
    {
        return NRF_ERROR_NULL;
    }

    access_model_add_params_t init_params =
    {
        .model_id = ACCESS_MODEL_SIG(custom_SERVER_MODEL_ID),
        .element_index =  element_index,
        .p_opcode_handlers = &m_opcode_handlers[0],
        .opcode_count = ARRAY_SIZE(m_opcode_handlers),
        .p_args = p_server,
        .publish_timeout_cb = periodic_publish_cb
    };

    uint32_t status = access_model_add(&init_params, &p_server->model_handle);

    if (status == NRF_SUCCESS)
    {
        status = access_model_subscription_list_alloc(p_server->model_handle);
    }

    return status;
}

uint32_t custom_server_status_publish(custom_server_t * p_server, const custom_status_params_t * p_params)
{
    if (p_server == NULL ||
        p_params == NULL)
    {
        return NRF_ERROR_NULL;
    }

    return status_send(p_server, NULL, p_params);
}
