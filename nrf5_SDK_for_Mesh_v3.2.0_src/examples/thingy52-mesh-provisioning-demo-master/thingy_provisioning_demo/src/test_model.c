
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

#include "test_model.h"
#include "model_common.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "access.h"
#include "access_config.h"
#include "nrf_mesh_assert.h"
#include "nrf_mesh_utils.h"
#include "nordic_common.h"

#include "log.h"
#include "device_manager.h"

/** Opcode Handlers */

static void status_handle(access_model_handle_t handle, const access_message_rx_t * p_rx_msg, void * p_args)
{
    //custom_client_t * p_client = (custom_client_t *) p_args;
    //custom_status_params_t in_data = {0};

    //if (p_rx_msg->length == GENERIC_ONOFF_STATUS_MINLEN || p_rx_msg->length == GENERIC_ONOFF_STATUS_MAXLEN)
    //{
    //    custom_status_msg_pkt_t * p_msg_params_packed = (custom_status_msg_pkt_t *) p_rx_msg->p_data;

    //    if (p_rx_msg->length == GENERIC_ONOFF_STATUS_MINLEN)
    //    {
    //        in_data.temp_intager = p_msg_params_packed->temp_intager;
    //        in_data.temp_decimal = p_msg_params_packed->temp_decimal;
    //        in_data.humi = p_msg_params_packed->humi;
    //        in_data.ttl = p_msg_params_packed->ttl;
    //        strcpy(in_data.addr, p_msg_params_packed->addr);
    //        //in_data.addr = p_msg_params_packed->addr;
    //    }
    //    else
    //    {
    //        in_data.temp_intager = p_msg_params_packed->temp_intager;
    //        in_data.temp_decimal = p_msg_params_packed->temp_decimal;
    //        in_data.humi = p_msg_params_packed->humi;
    //        in_data.ttl = p_msg_params_packed->ttl;
    //        strcpy(in_data.addr, p_msg_params_packed->addr);
    //        //in_data.addr = p_msg_params_packed->addr;
    //    }

    //    p_client->settings.p_callbacks->onoff_status_cb(p_client, &p_rx_msg->meta_data, &in_data);
    //}
}

static const access_opcode_handler_t m_opcode_handlers[] =
{
    {ACCESS_OPCODE_SIG(GENERIC_ONOFF_OPCODE_STATUS), status_handle},
};

static uint8_t message_set_packet_create(custom_set_msg_pkt_t *p_set, const custom_set_params_t * p_params)
{
        p_set->temp_intager = p_params->temp_intager;
        p_set->temp_decimal = p_params->temp_decimal;
        p_set->humi = p_params->humi;
        p_set->ttl = p_params->ttl;
        p_set->tid = p_params->tid;
        strcpy(p_set->addr, p_params->addr);
        
        return CUSTOM_SET_MAXLEN;
}

static void message_create(custom_client_t * p_client, uint16_t tx_opcode, const uint8_t * p_buffer,
                           uint16_t length, access_message_tx_t *p_message)
{
    p_message->opcode.opcode = tx_opcode;
    p_message->opcode.company_id = ACCESS_COMPANY_ID_NONE;
    p_message->p_buffer = p_buffer;
    p_message->length = length;
    p_message->force_segmented = p_client->settings.force_segmented;
    p_message->transmic_size = p_client->settings.transmic_size;
    p_message->access_token = nrf_mesh_unique_token_get();
}

static void reliable_context_create(custom_client_t * p_client, uint16_t reply_opcode,
                                    access_reliable_t * p_reliable)
{
    p_reliable->model_handle = p_client->model_handle;
    p_reliable->reply_opcode.opcode = reply_opcode;
    p_reliable->reply_opcode.company_id = ACCESS_COMPANY_ID_NONE;
    p_reliable->timeout = p_client->settings.timeout;
    p_reliable->status_cb = p_client->settings.p_callbacks->ack_transaction_status_cb;
}

/** Interface functions */

uint32_t custom_client_init(custom_client_t * p_client, uint8_t element_index)
{
    if (p_client == NULL ||
        p_client->settings.p_callbacks == NULL ||
        p_client->settings.p_callbacks->onoff_status_cb == NULL ||
        p_client->settings.p_callbacks->periodic_publish_cb == NULL)
    {
        return NRF_ERROR_NULL;
    }

    if (p_client->settings.timeout == 0)
    {
        p_client->settings.timeout= MODEL_ACKNOWLEDGED_TRANSACTION_TIMEOUT;
    }

    access_model_add_params_t add_params =
    {
        .model_id = ACCESS_MODEL_SIG(CUSTOM_CLIENT_MODEL_ID),
        .element_index = element_index,
        .p_opcode_handlers = &m_opcode_handlers[0],
        .opcode_count = ARRAY_SIZE(m_opcode_handlers),
        .p_args = p_client,
        .publish_timeout_cb = p_client->settings.p_callbacks->periodic_publish_cb
    };
    
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d \n", element_index);

    uint32_t status = access_model_add(&add_params, &p_client->model_handle);

    if (status == NRF_SUCCESS)
    {
        status = access_model_subscription_list_alloc(p_client->model_handle);
    }

    return status;
}

uint32_t custom_client_set(custom_client_t * p_client, const custom_set_params_t * p_params,
                                  const model_transition_t * p_transition)
{
    if (p_client == NULL || p_params == NULL)
    {
        return NRF_ERROR_NULL;
    }

    if (p_transition != NULL &&
        (p_transition->transition_time_ms > TRANSITION_TIME_MAX_MS ||
         p_transition->delay_ms > DELAY_TIME_MAX_MS))
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    if (access_reliable_model_is_free(p_client->model_handle))
    {
        uint8_t server_msg_length = message_set_packet_create(&p_client->msg_pkt.set, p_params);

        message_create(p_client, GENERIC_ONOFF_OPCODE_SET, (const uint8_t *) &p_client->msg_pkt.set,
                       server_msg_length, &p_client->access_message.message);
        reliable_context_create(p_client, GENERIC_ONOFF_OPCODE_STATUS, &p_client->access_message);

        return access_model_reliable_publish(&p_client->access_message);
    }
    else
    {
        return NRF_ERROR_BUSY;
    }
}

uint32_t custom_client_set_unack(custom_client_t * p_client, const custom_set_params_t * p_params,
                                         uint8_t repeats)
{
    uint32_t status = NRF_ERROR_INTERNAL;

    if (p_client == NULL || p_params == NULL) // null check
    {
        return NRF_ERROR_NULL;
    }

    if(p_params->ttl == 0)
    {
      __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "ttl is 0!\n");
      return NRF_ERROR_TIMEOUT;
    }

    uint8_t server_msg_length = message_set_packet_create(&p_client->msg_pkt.set, p_params); // 데이터의 길이를 받아온다
    //p_params안의 상태데이터(      on off)를  p_client속의   access_message_tx_t 형태로 바꾸게 된다.

    message_create(p_client, GENERIC_ONOFF_OPCODE_SET_UNACKNOWLEDGED,
                   (const uint8_t *) &p_client->msg_pkt.set, server_msg_length,
                   &p_client->access_message.message);
    

     __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "t_data = %2d.%-2d h_data = %2d tid = %d ttl = %d\n", 
                                          p_client->msg_pkt.set.temp_intager, p_client->msg_pkt.set.temp_decimal, p_client->msg_pkt.set.humi, 
                                          p_client->msg_pkt.set.tid, p_client->msg_pkt.set.ttl);

    print_addr(p_client->msg_pkt.set.addr);

    for (uint32_t i = 0; i <= repeats; ++i)
    {
        
        //status = custom_access_model_publish(p_client->model_handle, 
        //                                      &p_client->access_message.message, 
        //                                      p_client->msg_pkt.set.ttl);

        status = access_model_publish(p_client->model_handle, 
                                              &p_client->access_message.message);


        //uint8_t ttlll;
        //access_model_publish_ttl_get(p_client->model_handle, &ttlll);
        ////__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "status : %d\n", status);
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "status : %d ttl : %d\n", status, ttlll);
        
        if (status != NRF_SUCCESS)
        {
            break;
        }
    }
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "\n");
    return status;
}

uint32_t custom_client_get(custom_client_t * p_client)
{
    if (p_client == NULL)
    {
        return NRF_ERROR_NULL;
    }

    if (access_reliable_model_is_free(p_client->model_handle))
    {
        message_create(p_client, GENERIC_ONOFF_OPCODE_GET, NULL, 0, &p_client->access_message.message);
        reliable_context_create(p_client, GENERIC_ONOFF_OPCODE_STATUS, &p_client->access_message);

        return access_model_reliable_publish(&p_client->access_message);
    }
    else
    {
        return NRF_ERROR_BUSY;
    }
}

