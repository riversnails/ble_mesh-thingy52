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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "log.h"

#include "access.h"
#include "access_internal.h"
#include "access_config.h"
#include "access_publish_retransmission.h"

#include "access_publish.h"
#include "access_reliable.h"
#include "access_utils.h"

#include "nrf_mesh_assert.h"
#include "nrf_mesh_events.h"
#include "nrf_mesh_utils.h"
#include "nrf_mesh_externs.h"
#include "nrf_mesh.h"

#include "mesh_mem.h"
#include "device_state_manager.h"
#include "log.h"
#include "bitfield.h"
#include "timer.h"
#include "toolchain.h"
#include "event.h"
#include "bearer_event.h"
#if PERSISTENT_STORAGE
#include "flash_manager.h"
#endif

/*lint -e415 -e416 Lint fails to understand the boundary checking used for handles in this module (MBTLE-1831). */

/** Access model pool. @ref ACCESS_MODEL_COUNT is set by user at compile time. */
static access_common_t m_model_pool[ACCESS_MODEL_COUNT];

/** Access element pool. @ref ACCESS_ELEMENT_COUNT is set by user at compile time. */
static access_element_t m_element_pool[ACCESS_ELEMENT_COUNT];

/** Access subscription list pool. Makes it possible to share a subscription list  */
static access_subscription_list_t m_subscription_list_pool[ACCESS_SUBSCRIPTION_LIST_COUNT];

/** Mesh event handler. */
static nrf_mesh_evt_handler_t m_evt_handler;

/** Default TTL value for the node. */
static uint8_t m_default_ttl = ACCESS_DEFAULT_TTL;

/** Flag indicating that the device composition is frozen. */
static bool m_access_model_config_frozen;

/* ********** Static asserts ********** */

NRF_MESH_STATIC_ASSERT(ACCESS_MODEL_COUNT > 0);
NRF_MESH_STATIC_ASSERT(ACCESS_ELEMENT_COUNT > 0);
NRF_MESH_STATIC_ASSERT(ACCESS_PUBLISH_RESOLUTION_MAX <=
                       ((1 << ACCESS_PUBLISH_STEP_RES_BITS) - 1));
NRF_MESH_STATIC_ASSERT(ACCESS_PUBLISH_PERIOD_STEP_MAX <=
                       ((1 << ACCESS_PUBLISH_STEP_NUM_BITS) - 1));

/* ********** Static functions ********** */

static inline access_model_handle_t find_available_model(void)
{
    for (unsigned i = 0; i < ACCESS_MODEL_COUNT; ++i)
    {
        if (!ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[i].internal_state))
        {
            return i;
        }
    }
    return ACCESS_HANDLE_INVALID;
}

static void increment_model_count(uint16_t element_index, uint16_t model_company_id)
{
    if (model_company_id == ACCESS_COMPANY_ID_NONE)
    {
        m_element_pool[element_index].sig_model_count++;
    }
    else
    {
        m_element_pool[element_index].vendor_model_count++;
    }
}

static bool element_has_model_id(uint16_t element_index, access_model_id_t model_id, access_model_handle_t * p_model_handle)
{
    if ((m_element_pool[element_index].sig_model_count +
         m_element_pool[element_index].vendor_model_count) > 0)
    {
        for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
        {
            if (m_model_pool[i].model_info.element_index       == element_index &&
                m_model_pool[i].model_info.model_id.model_id   == model_id.model_id &&
                m_model_pool[i].model_info.model_id.company_id == model_id.company_id)
            {
                *p_model_handle = i;
                return true;
            }
        }
    }
    return false;
}

static access_opcode_t opcode_get(const uint8_t * p_buffer)
{
    access_opcode_t opcode = {0};
    switch (p_buffer[0] & ACCESS_PACKET_OPCODE_FORMAT_MASK)
    {
        case ACCESS_PACKET_OPCODE_FORMAT_1BYTE0:
        case ACCESS_PACKET_OPCODE_FORMAT_1BYTE1:
            opcode.opcode = p_buffer[0];
            opcode.company_id = ACCESS_COMPANY_ID_NONE;
            break;
        case ACCESS_PACKET_OPCODE_FORMAT_2BYTE:
            opcode.opcode = ((uint16_t) p_buffer[0] << 8) | (p_buffer[1]);
            opcode.company_id = ACCESS_COMPANY_ID_NONE;
            break;
        case ACCESS_PACKET_OPCODE_FORMAT_3BYTE:
            opcode.opcode = p_buffer[0];
            opcode.company_id = (p_buffer[1]) | ((uint16_t) p_buffer[2] << 8);
            break;
    }
    return opcode;
}

static void opcode_set(access_opcode_t opcode, uint8_t * p_buffer)
{
    if (opcode.company_id != ACCESS_COMPANY_ID_NONE)
    {
        p_buffer[0] = opcode.opcode & 0x00FF;
        p_buffer[1] = opcode.company_id & 0x00FF;
        p_buffer[2] = (opcode.company_id >> 8) & 0x00FF;
    }
    else if ((opcode.opcode & 0xFF00) > 0)
    {
        p_buffer[0] = (opcode.opcode >> 8) & 0x00FF;
        p_buffer[1] = opcode.opcode & 0x00FF;
    }
    else
    {
        p_buffer[0] = opcode.opcode & 0x00FF;
    }
}

static bool is_valid_opcode(access_opcode_t opcode)
{
    /** See opcode format table in @ref access_opcode_t documentation. */
    if (opcode.company_id == ACCESS_COMPANY_ID_NONE)
    {
        if ((opcode.opcode & 0xFF00) > 0)
        {
            /* Upper bits set to 0b10xxxxxx xxxxxxxx */
            return ((opcode.opcode & 0xC000) == 0x8000);
        }
        else
        {
            /* Upper bit set to 0b0xxxxxx, and not 0x01111111*/
            return ((opcode.opcode & 0xFF80) == 0x00) && (opcode.opcode != 0x7F);
        }
    }
    else
    {
        /* Two upper bits set to 0b11xxxxxx */
        return ((opcode.opcode & 0xFFC0) == 0x00C0);
    }
}

static bool opcodes_are_valid(const access_opcode_handler_t * p_opcode_handlers, uint32_t opcode_count)
{
    for (uint32_t i = 0; i < opcode_count; ++i)
    {
        if (!is_valid_opcode(p_opcode_handlers[i].opcode))
        {
            return false;
        }
    }
    return true;
}

static bool is_opcode_of_model(access_common_t * p_model, access_opcode_t opcode, uint32_t * p_opcode_index)
{
    for (uint32_t i = 0; i < p_model->opcode_count; ++i)
    {
        if (p_model->p_opcode_handlers[i].opcode.opcode     == opcode.opcode     &&
            p_model->p_opcode_handlers[i].opcode.company_id == opcode.company_id)
        {
            *p_opcode_index = i;
            return true;
        }
    }
    return false;
}

static inline bool model_handle_valid_and_allocated(access_model_handle_t handle)
{
    return (handle < ACCESS_MODEL_COUNT && ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[handle].internal_state));
}

static void mesh_msg_handle(const nrf_mesh_evt_message_t * p_evt)
{
    NRF_MESH_ASSERT(p_evt != NULL);
    access_opcode_t opcode = opcode_get(p_evt->p_buffer);
    if (opcode.opcode == ACCESS_OPCODE_INVALID)
    {
        return;
    }
    __LOG(LOG_SRC_ACCESS, LOG_LEVEL_DBG1, "RX: [aop: 0x%04x]\n", opcode.opcode);

    /*lint -save -e64 -e446 Side effects and type mismatches in initializer */
    dsm_handle_t appkey_handle = dsm_appkey_handle_get(p_evt->secmat.p_app);
    dsm_handle_t subnet_handle = dsm_subnet_handle_get(p_evt->secmat.p_net);
    access_message_rx_t message =
    {
        .opcode = opcode,
        .p_data = &p_evt->p_buffer[access_utils_opcode_size_get(opcode)],
        .length = p_evt->length - access_utils_opcode_size_get(opcode),
        .meta_data.src.type = p_evt->src.type,
        .meta_data.src.value = p_evt->src.value,
        .meta_data.src.p_virtual_uuid = p_evt->src.p_virtual_uuid,
        .meta_data.dst.type = p_evt->dst.type,
        .meta_data.dst.value = p_evt->dst.value,
        .meta_data.dst.p_virtual_uuid = p_evt->dst.p_virtual_uuid,
        .meta_data.ttl = p_evt->ttl,
        .meta_data.appkey_handle = appkey_handle,
        .meta_data.p_core_metadata = p_evt->p_metadata,
        .meta_data.subnet_handle = subnet_handle,
    };
    /*lint -restore */

    __LOG_XB(LOG_SRC_ACCESS, LOG_LEVEL_DBG1, "RX: Msg", message.p_data, message.length);

    access_incoming_handle(&message);
}

static void mesh_evt_cb(const nrf_mesh_evt_t * p_evt)
{
    switch (p_evt->type)
    {
        case NRF_MESH_EVT_MESSAGE_RECEIVED:
            mesh_msg_handle(&p_evt->params.message);
            break;
        default:
            /* Ignore */
            break;
    }

}

static bool check_tx_params(access_model_handle_t handle, const access_message_tx_t * p_tx_message, const access_message_rx_t * p_rx_message, uint32_t * p_status)
{
    NRF_MESH_ASSERT(NULL != p_status);

    /* Various checks being done here to ensure that model does not publish a message if
       publication is disabled by setting - Unassigned publish address, or deleted app key */
    if (p_tx_message->length >= ACCESS_MESSAGE_LENGTH_MAX)
    {
        *p_status = NRF_ERROR_INVALID_LENGTH;
    }
    else if (!model_handle_valid_and_allocated(handle) ||
             m_model_pool[handle].model_info.element_index >= ACCESS_ELEMENT_COUNT)
    {
        *p_status = NRF_ERROR_NOT_FOUND;
    }
    else if ((p_rx_message == NULL &&
             (m_model_pool[handle].model_info.publish_appkey_handle  == DSM_HANDLE_INVALID ||
              m_model_pool[handle].model_info.publish_address_handle == DSM_HANDLE_INVALID)) ||
              !is_valid_opcode(p_tx_message->opcode))
    {
        *p_status = NRF_ERROR_INVALID_PARAM;
    }
    else
    {
        *p_status = NRF_SUCCESS;
    }

    return (NRF_SUCCESS == *p_status);
}

static uint32_t packet_tx(access_model_handle_t handle,
                          const access_message_tx_t * p_tx_message,
                          const access_message_rx_t * p_rx_message,
                          const uint8_t *p_access_payload,
                          uint16_t access_payload_len)
{
    uint32_t status = NRF_SUCCESS;

    dsm_local_unicast_address_t local_addresses;
    dsm_local_unicast_addresses_get(&local_addresses);
    if (m_model_pool[handle].model_info.element_index >= local_addresses.count)
    {
        return NRF_ERROR_INVALID_ADDR;
    }

    uint16_t src_address = local_addresses.address_start + m_model_pool[handle].model_info.element_index;

    nrf_mesh_address_t dst_address;
    dsm_handle_t appkey_handle;
    dsm_handle_t subnet_handle = DSM_HANDLE_INVALID;
    if (p_rx_message != NULL)
    {
        appkey_handle = p_rx_message->meta_data.appkey_handle;
        subnet_handle = p_rx_message->meta_data.subnet_handle;
        dst_address = p_rx_message->meta_data.src;
    }
    else
    {
        appkey_handle = m_model_pool[handle].model_info.publish_appkey_handle;
        if (dsm_address_get(m_model_pool[handle].model_info.publish_address_handle, &dst_address) != NRF_SUCCESS)
        {
            return NRF_ERROR_NOT_FOUND;
        }
        if  (dst_address.value == NRF_MESH_ADDR_UNASSIGNED)
        {
            return NRF_ERROR_INVALID_ADDR;
        }
    }

    uint8_t ttl;
    if (p_rx_message != NULL && p_rx_message->meta_data.ttl == 0)
    {
        ttl = 0;
    }
    else if (m_model_pool[handle].model_info.publish_ttl == ACCESS_TTL_USE_DEFAULT)
    {
        ttl = m_default_ttl;
    }
    else
    {
        ttl = m_model_pool[handle].model_info.publish_ttl;
    }
    // ---------------------------------------------------------------------------------
    //ttl = 1;//m_model_pool[handle].model_info.publish_ttl; 
    // ---------------------------------------------------------------------------------

    NRF_MESH_ASSERT_DEBUG(p_access_payload != NULL);
    NRF_MESH_ASSERT_DEBUG(access_payload_len != 0);

    nrf_mesh_tx_params_t tx_params;
    memset(&tx_params, 0, sizeof(tx_params));

#if MESH_FEATURE_LPN_ENABLED
    status = NRF_ERROR_NOT_FOUND;
    if (m_model_pool[handle].model_info.friendship_credential_flag)
    {
        status = dsm_tx_friendship_secmat_get(subnet_handle, appkey_handle, &tx_params.security_material);
    }

    /* The Mesh Profile Specification v1.0, Section 4.2.2.4:
     *
     * When Publish Friendship Credential Flag is set to 1 and the friendship security material is
     * not available, the master security material shall be used. */
    if (NRF_ERROR_NOT_FOUND == status)
#endif
    {
        status = dsm_tx_secmat_get(subnet_handle, appkey_handle, &tx_params.security_material);
    }

    if (status != NRF_SUCCESS)
    {
        return status;
    }

    tx_params.dst = dst_address;
    tx_params.src = src_address;
    tx_params.ttl = ttl;
    tx_params.force_segmented = p_tx_message->force_segmented;
    tx_params.transmic_size = p_tx_message->transmic_size;
    tx_params.p_data = p_access_payload;
    tx_params.data_len = access_payload_len;
    tx_params.tx_token = p_tx_message->access_token;

    status = nrf_mesh_packet_send(&tx_params, NULL);
    if (status == NRF_SUCCESS)
    {
        __LOG(LOG_SRC_ACCESS, LOG_LEVEL_DBG1, "TX: [aop: 0x%04x] \n", p_tx_message->opcode.opcode);
        __LOG_XB(LOG_SRC_ACCESS, LOG_LEVEL_DBG1, "TX: Msg", p_tx_message->p_buffer, p_tx_message->length);
    }

    return status;
}

static uint32_t packet_alloc_and_tx(access_model_handle_t handle,
                                    const access_message_tx_t * p_tx_message,
                                    const access_message_rx_t * p_rx_message,
                                    uint8_t **pp_access_payload,
                                    uint16_t *p_access_payload_len)
{
    NRF_MESH_ASSERT_DEBUG(p_tx_message != NULL);

    uint32_t status;

    if (!check_tx_params(handle, p_tx_message, p_rx_message, &status))
    {
        return status;
    }

    uint16_t opcode_length = access_utils_opcode_size_get(p_tx_message->opcode);
    uint16_t payload_length = opcode_length + p_tx_message->length;

    uint8_t *p_payload = (uint8_t *) mesh_mem_alloc(payload_length);
    if (NULL == p_payload)
    {
        return NRF_ERROR_NO_MEM;
    }

    opcode_set(p_tx_message->opcode, p_payload);
    memcpy(&p_payload[opcode_length], p_tx_message->p_buffer, p_tx_message->length);

    status = packet_tx(handle, p_tx_message, p_rx_message, p_payload, payload_length);
    if (NRF_SUCCESS != status || NULL == pp_access_payload || NULL == p_access_payload_len)
    {
        mesh_mem_free(p_payload);
        return status;
    }

    *pp_access_payload = p_payload;
    *p_access_payload_len = payload_length;

    return NRF_SUCCESS;
}

static void access_state_clear(void)
{
    memset(&m_model_pool[0], 0, sizeof(m_model_pool));
    memset(&m_element_pool[0], 0, sizeof(m_element_pool));
    memset(&m_subscription_list_pool[0], 0, sizeof(m_subscription_list_pool));
    for (uint16_t i = 0; i < ARRAY_SIZE(m_model_pool); ++i)
    {
        m_model_pool[i].model_info.publish_address_handle = DSM_HANDLE_INVALID;
        m_model_pool[i].model_info.publish_appkey_handle = DSM_HANDLE_INVALID;
        m_model_pool[i].model_info.subscription_pool_index = ACCESS_SUBSCRIPTION_LIST_COUNT;
        m_model_pool[i].model_info.element_index = ACCESS_ELEMENT_INDEX_INVALID;
        m_model_pool[i].publish_divisor = 1;
    }
}

static bool model_subscribes_to_addr(const access_common_t * p_model, dsm_handle_t address_handle)
{
    return (p_model->model_info.subscription_pool_index < ACCESS_SUBSCRIPTION_LIST_COUNT &&
            bitfield_get(m_subscription_list_pool[p_model->model_info.subscription_pool_index].bitfield,
                         address_handle));
}

/**
 * Checks whether a destination address is designated for a specific element.
 *
 * @param[in] p_dst The destination address to check for
 * @param[in,out] p_index Index of the element that should process the message destined for the given address.
 *
 * @returns Whether or not the destination address is designated for a specific element.
 */
static bool is_element_rx_address(const nrf_mesh_address_t * p_dst, uint16_t * p_index)
{
    if (p_dst->type == NRF_MESH_ADDRESS_TYPE_UNICAST)
    {
        dsm_local_unicast_address_t local_addresses;
        dsm_local_unicast_addresses_get(&local_addresses);

        *p_index = p_dst->value - local_addresses.address_start;
        return true;
    }
    else if (p_dst->value == NRF_MESH_ALL_PROXIES_ADDR ||
             p_dst->value == NRF_MESH_ALL_FRIENDS_ADDR ||
             p_dst->value == NRF_MESH_ALL_RELAYS_ADDR ||
             p_dst->value == NRF_MESH_ALL_NODES_ADDR)
    {
        /* According to the Mesh Profile Specification v1.0 section 3.4.2, only models on the
         * primary element of the node shall process the fixed group addresses. */
        *p_index = 0;
        return true;
    }
    return false;
}

/* Checks if the model's subscription list is shared. Note: Model handle must be valid */
static bool model_subscription_list_is_shared(access_model_handle_t handle)
{
    NRF_MESH_ASSERT_DEBUG(model_handle_valid_and_allocated(handle));
    uint16_t sub_pool_index = m_model_pool[handle].model_info.subscription_pool_index;

    for (uint32_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
    {
        if (m_model_pool[i].model_info.subscription_pool_index == sub_pool_index && i != handle)
        {
            return true;
        }
    }

    return false;
}

/* Calculates the value of the fast publish interval. Minimum possible output interval will be 100 ms. */
static void divide_publish_interval(access_publish_resolution_t input_resolution, uint8_t input_steps,
        access_publish_resolution_t * p_output_resolution, uint8_t * p_output_steps, uint16_t divisor)
{
    uint32_t ms100_value = input_steps;

    /* Convert the step value to 100 ms steps depending on the resolution: */
    switch (input_resolution)
    {
        case ACCESS_PUBLISH_RESOLUTION_100MS:
            break;
        case ACCESS_PUBLISH_RESOLUTION_1S:
            ms100_value *= 10;
            break;
        case ACCESS_PUBLISH_RESOLUTION_10S:
            ms100_value *= 100;
            break;
        case ACCESS_PUBLISH_RESOLUTION_10MIN:
            ms100_value *= 6000;
            break;
        default:
            NRF_MESH_ASSERT(false);
    }

    /* Divide the 100 ms value with the fast divisor: */
    ms100_value /= divisor;

    /* Get the divisor corresponding to the new number of 100 ms steps: */
    if (ms100_value < 10)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_100MS;
        if (input_steps == 0)
        {
            *p_output_steps = 0;
        }
        else
        {
            /* Avoid accidentally disabling publishing by dividing by too high a number */
            *p_output_steps = MAX(1, ms100_value);
        }
    }
    else if (ms100_value < 100)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_1S;
        *p_output_steps = ms100_value / 10;
    }
    else if (ms100_value < 6000)
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_10S;
        *p_output_steps = ms100_value / 100;
    }
    else
    {
        *p_output_resolution = ACCESS_PUBLISH_RESOLUTION_10MIN;
        *p_output_steps = ms100_value / 6000;
    }

}

static void access_publish_timing_update(access_model_handle_t handle)
{
    access_publish_resolution_t reg_res, fast_res;
    uint8_t reg_steps, fast_steps;

    NRF_MESH_ASSERT(access_model_publish_period_get(handle, &reg_res, &reg_steps) == NRF_SUCCESS);
    divide_publish_interval(reg_res, reg_steps, &fast_res, &fast_steps, m_model_pool[handle].publish_divisor);
    access_publish_period_set(&m_model_pool[handle].publication_state, fast_res, fast_steps);
}

#if PERSISTENT_STORAGE
/** Global flag to keep track of if the flash metadata is up to date.*/
static bool m_metadata_stored;

/** Global flag to keep track of if the flash_manager is not ready for use (being erased or not added) .*/
static bool m_flash_not_ready;

/* The flash manager instance used by this module. */
static flash_manager_t m_flash_manager;

NRF_MESH_STATIC_ASSERT(ACCESS_MODEL_COUNT < FLASH_HANDLE_TO_ACCESS_HANDLE_MASK);
NRF_MESH_STATIC_ASSERT(ACCESS_ELEMENT_COUNT < FLASH_HANDLE_TO_ACCESS_HANDLE_MASK);
NRF_MESH_STATIC_ASSERT(ACCESS_SUBSCRIPTION_LIST_COUNT < FLASH_HANDLE_TO_ACCESS_HANDLE_MASK);
/* We have used ACCESS_MODEL_STATE_FLASH_SIZE as the LARGEST_ENTRY_SIZE in the FLASH_MANAGER_PAGE_COUNT_MINIMUM macro below so verify: */
NRF_MESH_STATIC_ASSERT(ACCESS_SUBS_LIST_FLASH_SIZE < ACCESS_MODEL_STATE_FLASH_SIZE && ACCESS_MODEL_STATE_FLASH_SIZE > ACCESS_ELEMENTS_FLASH_SIZE);
/* Verify that the ACCESS_FLASH_PAGE_COUNT is of sufficient size. */
NRF_MESH_STATIC_ASSERT(FLASH_MANAGER_PAGE_COUNT_MINIMUM(ACCESS_FLASH_ENTRY_SIZE, ACCESS_MODEL_STATE_FLASH_SIZE) <= ACCESS_FLASH_PAGE_COUNT);

static inline void mark_all_as_outdated()
{
    /* Mark all allocated subscription lists as outdated. */
    for (uint16_t i = 0; i < ACCESS_SUBSCRIPTION_LIST_COUNT; ++i)
    {
        if (ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_subscription_list_pool[i].internal_state))
        {
            ACCESS_INTERNAL_STATE_OUTDATED_SET(m_subscription_list_pool[i].internal_state);
        }
    }
    /* Mark all elements as outdated. */
    for (uint16_t i = 0; i < ACCESS_ELEMENT_COUNT; ++i)
    {
        if (m_element_pool[i].location != 0)
        {
            ACCESS_INTERNAL_STATE_OUTDATED_SET(m_element_pool[i].internal_state);
        }
    }
    /* Mark all allocated models as outdated. */
    for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
    {
        if (ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[i].internal_state))
        {
            ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[i].internal_state);
        }
    }
}

static void flash_write_complete(const flash_manager_t * p_manager, const fm_entry_t * p_entry, fm_result_t result)
{
    /* If we get an AREA_FULL then our calculations for flash space required are buggy. */
    NRF_MESH_ASSERT(result != FM_RESULT_ERROR_AREA_FULL);
    /* We do not invalidate in this module, so a NOT_FOUND should not be received. */
    NRF_MESH_ASSERT(result != FM_RESULT_ERROR_NOT_FOUND);
    if (result == FM_RESULT_ERROR_FLASH_MALFUNCTION)
    {
        /* Let the user know that the flash is dying. */
        nrf_mesh_evt_t evt = {
            .type = NRF_MESH_EVT_FLASH_FAILED,
            .params.flash_failed.user = NRF_MESH_FLASH_USER_ACCESS,
            .params.flash_failed.p_flash_entry = p_entry,
            .params.flash_failed.p_flash_page = NULL,
            .params.flash_failed.p_area = p_manager->config.p_area,
            .params.flash_failed.page_count = p_manager->config.page_count
        };
        event_handle(&evt);
    }
}

static void flash_invalidate_complete(const flash_manager_t * p_manager, fm_handle_t handle, fm_result_t result)
{
    /* Expect no invalidate complete calls. */
    NRF_MESH_ASSERT(false);
}

typedef void (*flash_op_func_t) (void);
static void flash_manager_mem_available(void * p_args)
{
    ((flash_op_func_t) p_args)(); /*lint !e611 Suspicious cast */
}

static void add_flash_manager(void);
static void flash_remove_complete(const flash_manager_t * p_manager)
{
    mark_all_as_outdated();
    add_flash_manager();
}

static void add_flash_manager(void)
{

    static fm_mem_listener_t flash_add_mem_available_struct = {
        .callback = flash_manager_mem_available,
        .p_args = add_flash_manager
    };
    flash_manager_config_t manager_config;
    manager_config.write_complete_cb = flash_write_complete;
    manager_config.invalidate_complete_cb = flash_invalidate_complete;
    manager_config.remove_complete_cb = flash_remove_complete;
    manager_config.min_available_space = WORD_SIZE;
    manager_config.p_area = access_flash_area_get();
    manager_config.page_count = ACCESS_FLASH_PAGE_COUNT;
    m_flash_not_ready = true;
    uint32_t status = flash_manager_add(&m_flash_manager, &manager_config);
    if (NRF_SUCCESS != status)
    {
        flash_manager_mem_listener_register(&flash_add_mem_available_struct);
    }
    else
    {
        m_flash_not_ready = false;
    }
}
/**
 * Restore flash data with the given filter match.
 *
 * Calls the restore callback for every matching entry. The @p p_args argument in the restore
 * callback points to a @c fail boolean which the restore callback can use to indicate invalid entries.
 *
 * @param[in] filter_match Filter to match entries on, only the bits in @ref FLASH_HANDLE_FILTER_MASK are considered.
 * @param[in] restore_callback Callback to call on every matching entry.
 * @param[out] p_success Will be set @c true if the entries were successfully restored, and @c false
 * if one the entries couldn't be restored.
 *
 * @returns The number of restored entries.
 */
static uint32_t restore_flash_data(fm_handle_t filter_match, flash_manager_read_cb_t restore_callback, bool * p_success)
{
    fm_handle_filter_t filter = {.mask = FLASH_HANDLE_FILTER_MASK, .match = filter_match};
    *p_success = true;
    return flash_manager_entries_read(&m_flash_manager, &filter, restore_callback, p_success);
}

static fm_iterate_action_t restore_acquired_subscription(const fm_entry_t * p_entry, void * p_success)
{
    access_flash_subscription_list_t * p_subs_list_entry = (access_flash_subscription_list_t *) p_entry->data;
    uint16_t index = p_entry->header.handle & FLASH_HANDLE_TO_ACCESS_HANDLE_MASK;
    if (index >= ACCESS_SUBSCRIPTION_LIST_COUNT)
    {
        *((bool *) p_success) = false;
        return FM_ITERATE_ACTION_STOP;
    }
    ACCESS_INTERNAL_STATE_ALLOCATED_SET(m_subscription_list_pool[index].internal_state);
    for (uint32_t i = 0; i < ARRAY_SIZE(p_subs_list_entry->inverted_bitfield); ++i)
    {
        m_subscription_list_pool[index].bitfield[i] = ~p_subs_list_entry->inverted_bitfield[i];
    }
    return FM_ITERATE_ACTION_CONTINUE;
}

static inline bool restore_subscription_lists(void)
{
    bool success;
    (void) restore_flash_data(FLASH_GROUP_SUBS_LIST, restore_acquired_subscription, &success);
    return success;
}

static fm_iterate_action_t restore_acquired_element(const fm_entry_t * p_entry, void * p_success)
{
    uint16_t index = p_entry->header.handle & FLASH_HANDLE_TO_ACCESS_HANDLE_MASK;
    uint16_t * p_element_location = (uint16_t *) p_entry->data;
    if (index >= ACCESS_ELEMENT_COUNT)
    {
        *((bool *) p_success) = false;
        return FM_ITERATE_ACTION_STOP;
    }
    m_element_pool[index].location = *p_element_location;
    return FM_ITERATE_ACTION_CONTINUE;
}

static inline bool restore_elements(void)
{
    for (int i = 0; i < ACCESS_ELEMENT_COUNT; ++i)
    {
        m_element_pool[i].sig_model_count = 0;
        m_element_pool[i].vendor_model_count = 0;
        m_element_pool[i].location = 0;
        ACCESS_INTERNAL_STATE_OUTDATED_CLR(m_element_pool[i].internal_state);
    }
    bool success;
    (void) restore_flash_data(FLASH_GROUP_ELEMENT, restore_acquired_element, &success);
    return success;
}

static fm_iterate_action_t restore_acquired_model(const fm_entry_t * p_entry, void * p_success)
{
    access_model_state_data_t * p_model_data_entry = (access_model_state_data_t *) p_entry->data;
    uint16_t index = p_entry->header.handle & FLASH_HANDLE_TO_ACCESS_HANDLE_MASK;
    if (index >= ACCESS_MODEL_COUNT ||
        p_model_data_entry->element_index >= ACCESS_ELEMENT_COUNT ||
        (m_model_pool[index].model_info.subscription_pool_index < ACCESS_SUBSCRIPTION_LIST_COUNT &&
         (m_model_pool[index].model_info.subscription_pool_index != p_model_data_entry->subscription_pool_index ||
          !ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_subscription_list_pool[p_model_data_entry->subscription_pool_index].internal_state))))
    {
        /* Fail if:
         * - The model handle index is invalid
         * - The element index pointed to is invalid
         * - The model has an allocated subscription list, but the index doesn't match the one in
         *   flash or isn't allocated in the subscription list pool
         *
         * All of these conditions are signs that the user access init code has changed (order of
         * allocation etc.)
         */
        *((bool *) p_success) = false;
        return FM_ITERATE_ACTION_STOP;
    }

    if (p_model_data_entry->subscription_pool_index < ACCESS_SUBSCRIPTION_LIST_COUNT)
    {
        ACCESS_INTERNAL_STATE_RESTORED_SET(m_subscription_list_pool[p_model_data_entry->subscription_pool_index].internal_state);
    }
    memcpy(&m_model_pool[index].model_info, p_model_data_entry, sizeof(access_model_state_data_t));
    increment_model_count(p_model_data_entry->element_index, p_model_data_entry->model_id.company_id);
    return FM_ITERATE_ACTION_CONTINUE;
}

static inline void restore_addresses_for_model(const access_common_t * p_model)
{
    if (p_model->model_info.publish_address_handle != DSM_HANDLE_INVALID)
    {
        NRF_MESH_ERROR_CHECK(dsm_address_publish_add_handle(p_model->model_info.publish_address_handle));
    }

    if (p_model->model_info.subscription_pool_index != ACCESS_SUBSCRIPTION_LIST_COUNT)
    {
        const access_subscription_list_t * p_sub = &m_subscription_list_pool[p_model->model_info.subscription_pool_index];
        for (uint32_t i = bitfield_next_get(p_sub->bitfield, DSM_ADDR_MAX, 0);
             i != DSM_ADDR_MAX;
             i = bitfield_next_get(p_sub->bitfield, DSM_ADDR_MAX, i+1))
        {
            NRF_MESH_ERROR_CHECK(dsm_address_subscription_add_handle(i));
        }
    }
}

#if ACCESS_MODEL_PUBLISH_PERIOD_RESTORE
static inline void restore_publication_period(access_common_t * p_model)
{
    if (p_model->model_info.publication_period.step_num != 0)
    {
        access_publish_period_set(&p_model->publication_state,
                                  (access_publish_resolution_t) p_model->model_info.publication_period.step_res,
                                  p_model->model_info.publication_period.step_num);
    }
}
#endif

static inline bool restore_models(void)
{
    bool success;
    uint32_t restore_count = restore_flash_data(FLASH_GROUP_MODEL, restore_acquired_model, &success);
    if (success && restore_count > 0)
    {
        for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
        {
            if (m_model_pool[i].model_info.element_index != ACCESS_ELEMENT_INDEX_INVALID)
            {
                    restore_addresses_for_model(&m_model_pool[i]);

    #if ACCESS_MODEL_PUBLISH_PERIOD_RESTORE
                    restore_publication_period(&m_model_pool[i]);
    #else
                    m_model_pool[i].model_info.publication_period.step_res = 0;
                    m_model_pool[i].model_info.publication_period.step_num = 0;
    #endif
            }
        }
    }
    return success;
}

static inline uint32_t metadata_store(void)
{
    fm_entry_t * p_entry = flash_manager_entry_alloc(&m_flash_manager, FLASH_HANDLE_METADATA, sizeof(access_flash_metadata_t));

    if (p_entry == NULL)
    {
        return NRF_ERROR_BUSY;
    }
    else
    {
        access_flash_metadata_t * p_metadata = (access_flash_metadata_t *) p_entry->data;
        p_metadata->element_count = ACCESS_ELEMENT_COUNT;
        p_metadata->model_count = ACCESS_MODEL_COUNT;
        p_metadata->subscription_list_count = ACCESS_SUBSCRIPTION_LIST_COUNT;
        flash_manager_entry_commit(p_entry);
        m_metadata_stored = true;
        return NRF_SUCCESS;
    }
}

static inline uint32_t subscription_list_store(uint16_t index)
{
    fm_entry_t * p_entry = flash_manager_entry_alloc(&m_flash_manager, FLASH_GROUP_SUBS_LIST | index, sizeof(access_flash_subscription_list_t));

    if (p_entry == NULL)
    {
        return NRF_ERROR_BUSY;
    }
    else
    {
        access_flash_subscription_list_t * p_subs_list = (access_flash_subscription_list_t *) p_entry->data;
        for (uint32_t i = 0; i < ARRAY_SIZE(p_subs_list->inverted_bitfield); ++i)
        {
            p_subs_list->inverted_bitfield[i] = ~m_subscription_list_pool[index].bitfield[i];
        }
        flash_manager_entry_commit(p_entry);
        ACCESS_INTERNAL_STATE_OUTDATED_CLR(m_subscription_list_pool[index].internal_state);
        return NRF_SUCCESS;
    }
}

static inline uint32_t model_store(access_model_handle_t handle)
{
    fm_entry_t * p_entry = flash_manager_entry_alloc(&m_flash_manager, FLASH_GROUP_MODEL | handle, sizeof(access_model_state_data_t));

    if (p_entry == NULL)
    {
        return NRF_ERROR_BUSY;
    }
    else
    {
        access_model_state_data_t * p_model_data_entry = (access_model_state_data_t *) p_entry->data;
        memcpy(p_model_data_entry, &m_model_pool[handle].model_info, sizeof(access_model_state_data_t));
        flash_manager_entry_commit(p_entry);
        ACCESS_INTERNAL_STATE_OUTDATED_CLR(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

static inline uint32_t element_store(uint16_t element_index)
{
    fm_entry_t * p_entry = flash_manager_entry_alloc(&m_flash_manager, FLASH_GROUP_ELEMENT | element_index, sizeof(uint16_t));

    if (p_entry == NULL)
    {
        return NRF_ERROR_BUSY;
    }
    else
    {
        uint16_t * p_element_location = (uint16_t *) p_entry->data;
        *p_element_location = m_element_pool[element_index].location;
        flash_manager_entry_commit(p_entry);
        ACCESS_INTERNAL_STATE_OUTDATED_CLR(m_element_pool[element_index].internal_state);
        return NRF_SUCCESS;
    }
}


/* ********** Public API ********** */

static void access_flash_config_clear(void)
{
    static fm_mem_listener_t flash_clear_mem_available_struct =
        {
            .callback = flash_manager_mem_available,
            .p_args = access_flash_config_clear
        };
    m_metadata_stored = false;
    m_flash_not_ready = true;
    uint32_t status = flash_manager_remove(&m_flash_manager);
    if (NRF_SUCCESS != status)
    {
        flash_manager_mem_listener_register(&flash_clear_mem_available_struct);
    }
    else
    {
        m_access_model_config_frozen = false;
    }
}

bool access_flash_config_load(void)
{
    access_flash_metadata_t metadata;
    uint32_t metadata_size = sizeof(metadata);

    if (flash_manager_entry_read(&m_flash_manager, FLASH_HANDLE_METADATA, &metadata, &metadata_size) != NRF_SUCCESS)
    {
        /* Entry could not be read, means the application is trying to load access data for the
         * very first time when none exist, consider this as success and freeze further changes to
         * access model configuration.
         */
        m_access_model_config_frozen = true;
        return true;
    }

    bool config_restored = false;
    /* make sure that the stored flash data isn't too big for this firmware: */
    if (metadata.element_count == ACCESS_ELEMENT_COUNT &&
        metadata.model_count == ACCESS_MODEL_COUNT &&
        metadata.subscription_list_count == ACCESS_SUBSCRIPTION_LIST_COUNT)
    {
        m_metadata_stored = true;
        config_restored = restore_subscription_lists() && restore_elements() && restore_models();
    }

    if (!config_restored)
    {
        __LOG(LOG_SRC_ACCESS, LOG_LEVEL_ERROR, "Failed to restore configuration.\n");

        /* One invalid entry invalidates everything stored, wipe flash. */
        dsm_clear();
        access_flash_config_clear();
    }

    /* Freeze composition data. */
    m_access_model_config_frozen = true;

    return config_restored;
}

void access_flash_config_store(void)
{
    if (!m_access_model_config_frozen)
    {
        return;
    }

    uint32_t status = NRF_SUCCESS;
    /* Lock this call since it can be called by flash manager in bearer_event context in the listener callback.*/
    bearer_event_critical_section_begin();
    /* If flash is being erased, no need to store anything now. */
    if (m_flash_not_ready)
    {
        /* Setting status to something other than NRF_SUCCESS will end up calling flash_manager_mem_listener_register at the end of this function */
        status = NRF_ERROR_INVALID_STATE;
    }
    else if (!m_metadata_stored)
    {
        status = metadata_store();

    }
    /* Store all allocated subscription lists. */
    for (uint16_t i = 0; i < ACCESS_SUBSCRIPTION_LIST_COUNT && status == NRF_SUCCESS; ++i)
    {
        if (ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_subscription_list_pool[i].internal_state) &&
            ACCESS_INTERNAL_STATE_IS_OUTDATED(m_subscription_list_pool[i].internal_state))
        {
            status = subscription_list_store(i);
        }
    }
    /* Store all elements. */
    for (uint16_t i = 0; i < ACCESS_ELEMENT_COUNT && status == NRF_SUCCESS; ++i)
    {
        if (ACCESS_INTERNAL_STATE_IS_OUTDATED(m_element_pool[i].internal_state))
        {
            status = element_store(i);
        }
    }
    /* Store all allocated models. */
    for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT && status == NRF_SUCCESS; ++i)
    {
        if (ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[i].internal_state) &&
            ACCESS_INTERNAL_STATE_IS_OUTDATED(m_model_pool[i].internal_state))
        {
            status = model_store(i);
        }
    }

    if (NRF_SUCCESS != status)
    {
        static fm_mem_listener_t flash_store_mem_available_struct = {
            .callback = flash_manager_mem_available,
            .p_args = access_flash_config_store
        };
        flash_manager_mem_listener_register(&flash_store_mem_available_struct);
    }

    bearer_event_critical_section_end();
}
#else
static void access_flash_config_clear(void)
{
    m_access_model_config_frozen = false;
}
bool access_flash_config_load(void)
{
    return false;
}
void access_flash_config_store(void)
{
}

#endif /* PERSISTENT_STORAGE */

/* ********** Private API ********** */
void access_incoming_handle(const access_message_rx_t * p_message) // ?????? ???????????
{
    const nrf_mesh_address_t * p_dst = &p_message->meta_data.dst;

    if (nrf_mesh_is_address_rx(p_dst))
    {
        uint16_t element_index;
        dsm_handle_t address_handle = DSM_HANDLE_INVALID;
        bool is_element_message = is_element_rx_address(p_dst, &element_index);

        if (!is_element_message)
        {
            /* If it's not one of the element addresses, it has to be a subscription address. */
            NRF_MESH_ERROR_CHECK(dsm_address_handle_get(p_dst, &address_handle));
        }

        for (int i = 0; i < ACCESS_MODEL_COUNT; ++i)
        {
            access_common_t * p_model = &m_model_pool[i];
            uint32_t opcode_index;

            bool address_match =
                (is_element_message ? (p_model->model_info.element_index == element_index)
                                    : (model_subscribes_to_addr(p_model, address_handle)));

            bool model_allocated = ACCESS_INTERNAL_STATE_IS_ALLOCATED(p_model->internal_state);
            bool appkey_bound = bitfield_get(p_model->model_info.application_keys_bitfield, p_message->meta_data.appkey_handle);
            bool valid_opcode = is_opcode_of_model(p_model, p_message->opcode, &opcode_index);

            __LOG(LOG_SRC_ACCESS, LOG_LEVEL_DBG3, "cmp_id: 0x%04x mdl_id: 0x%04x  alloc? %d  addr_match? %d  key_bound? %d  opcode? %d\n",
                  p_model->model_info.model_id.company_id, p_model->model_info.model_id.model_id,
                  model_allocated, address_match, appkey_bound, valid_opcode);

            if (model_allocated && address_match && appkey_bound && valid_opcode)
            {
                if (p_dst->type == NRF_MESH_ADDRESS_TYPE_UNICAST)
                {
                    access_reliable_message_rx_cb(i, p_message, p_model->p_args);
                }
                
                //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", m_model_pool[i].model_info.publish_ttl);
                p_model->p_opcode_handlers[opcode_index].handler(i, p_message, p_model->p_args);
            }
        }
    }
}

uint32_t access_packet_tx(access_model_handle_t handle,
                          const access_message_tx_t * p_tx_message,
                          const uint8_t *p_access_payload,
                          uint16_t access_payload_len)
{
    uint32_t status;
    if (NULL == p_tx_message || NULL == p_access_payload || 0 == access_payload_len)
    {
        return NRF_ERROR_NULL;
    }
    else if (!check_tx_params(handle, p_tx_message, NULL, &status))
    {
        return status;
    }

    return packet_tx(handle, p_tx_message, NULL, p_access_payload,
                     access_payload_len);
}

/* ********** Public API ********** */
void access_clear(void)
{
    access_state_clear();
    access_reliable_cancel_all();
    access_flash_config_clear();
}

void access_init(void)
{
    access_state_clear();

    m_evt_handler.evt_cb = mesh_evt_cb;
    nrf_mesh_evt_handler_add(&m_evt_handler);
    access_reliable_init();
    access_publish_init();
    access_publish_retransmission_init();

    /* Initialize the flash manager */
    /* Assuming that we only need to play nice to DSM: */
    /* TODO: Move this into flash manager and check for proper overlaping flash regions. */
#if PERSISTENT_STORAGE
#ifdef ACCESS_FLASH_AREA_LOCATION
    NRF_MESH_ASSERT(ACCESS_FLASH_AREA_LOCATION + PAGE_SIZE * ACCESS_FLASH_PAGE_COUNT <= (uint32_t)dsm_flash_area_get());
#endif /* ACCESS_FLASH_AREA_LOCATION */
    add_flash_manager();
#endif /* PERSISTENT_STORAGE */

    m_access_model_config_frozen = false;
}

uint32_t access_model_add(const access_model_add_params_t * p_model_params,
                          access_model_handle_t * p_model_handle)
{
    if (NULL == p_model_params ||
        NULL == p_model_handle ||
        (0 != p_model_params->opcode_count && NULL == p_model_params->p_opcode_handlers))
    {
        return NRF_ERROR_NULL;
    }
    *p_model_handle = ACCESS_HANDLE_INVALID;

    if (0 == p_model_params->opcode_count && NULL != p_model_params->p_opcode_handlers)
    {
        return NRF_ERROR_INVALID_LENGTH;
    }
    else if (p_model_params->element_index >= ACCESS_ELEMENT_COUNT)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_access_model_config_frozen ||
             (element_has_model_id(p_model_params->element_index, p_model_params->model_id, p_model_handle) &&
             ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[*p_model_handle].internal_state))
            )
    {
        return NRF_ERROR_FORBIDDEN;
    }
    else if (!opcodes_are_valid(p_model_params->p_opcode_handlers, p_model_params->opcode_count))
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (*p_model_handle == ACCESS_HANDLE_INVALID) /* The model was not recovered from the flash */
    {
        *p_model_handle = find_available_model();
        if (ACCESS_HANDLE_INVALID == *p_model_handle)
        {
            return NRF_ERROR_NO_MEM;
        }

        m_model_pool[*p_model_handle].model_info.publish_address_handle = DSM_HANDLE_INVALID;
        m_model_pool[*p_model_handle].model_info.publish_appkey_handle = DSM_HANDLE_INVALID;
        m_model_pool[*p_model_handle].model_info.element_index = p_model_params->element_index;
        m_model_pool[*p_model_handle].model_info.model_id.model_id = p_model_params->model_id.model_id;
        m_model_pool[*p_model_handle].model_info.model_id.company_id = p_model_params->model_id.company_id;
        m_model_pool[*p_model_handle].model_info.publish_ttl = ACCESS_TTL_USE_DEFAULT;
        increment_model_count(p_model_params->element_index, p_model_params->model_id.company_id);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[*p_model_handle].internal_state);
    }

    m_model_pool[*p_model_handle].p_args = p_model_params->p_args;
    m_model_pool[*p_model_handle].p_opcode_handlers = p_model_params->p_opcode_handlers;
    m_model_pool[*p_model_handle].opcode_count = p_model_params->opcode_count;

    m_model_pool[*p_model_handle].publication_state.publish_timeout_cb = p_model_params->publish_timeout_cb;
    m_model_pool[*p_model_handle].publication_state.model_handle = *p_model_handle;
    ACCESS_INTERNAL_STATE_ALLOCATED_SET(m_model_pool[*p_model_handle].internal_state);

    return NRF_SUCCESS;
}

uint32_t access_model_publish(access_model_handle_t handle, const access_message_tx_t * p_message)
{
    uint32_t status;

    if (p_message == NULL)
    {
        return NRF_ERROR_NULL;
    }

    uint16_t payload_length = 0;
    uint8_t *p_payload;

    //uint32_t state;
    access_model_publish_ttl_set(handle, 2);
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "status : %d\n", state);

    status = packet_alloc_and_tx(handle, p_message, NULL, &p_payload, &payload_length);
    if (NRF_SUCCESS != status)
    {
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", status);
        return status;
    }

    if (m_model_pool[handle].model_info.publication_retransmit.count > 0)
    {
        access_publish_retransmission_message_add(handle,
                                                  &m_model_pool[handle].model_info.publication_retransmit,
                                                  p_message,
                                                  p_payload,
                                                  payload_length);
    }
    else
    {
        mesh_mem_free(p_payload);
    }
    return NRF_SUCCESS;
}


//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------
uint32_t custom_access_model_publish(access_model_handle_t handle, const access_message_tx_t * p_message, uint8_t ttl)
{
    uint32_t status;

    if (p_message == NULL)
    {
        return NRF_ERROR_NULL;
    }

    uint16_t payload_length = 0;
    uint8_t *p_payload;

    //uint32_t state;
    //access_model_publish_ttl_set(handle, ttl);
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "status : %d\n", state);

    status = packet_alloc_and_tx(handle, p_message, NULL, &p_payload, &payload_length);
    if (NRF_SUCCESS != status)
    {
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", status);
        return status;
    }

    if (m_model_pool[handle].model_info.publication_retransmit.count > 0)
    {
        access_publish_retransmission_message_add(handle,
                                                  &m_model_pool[handle].model_info.publication_retransmit,
                                                  p_message,
                                                  p_payload,
                                                  payload_length);
    }
    else
    {
        mesh_mem_free(p_payload);
    }
    return NRF_SUCCESS;

    return access_model_publish(handle, p_message);
}
//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------


uint32_t access_model_reply(access_model_handle_t handle,
                            const access_message_rx_t * p_message,
                            const access_message_tx_t * p_reply)
{
    if (p_message == NULL || p_reply == NULL)
    {
        return NRF_ERROR_NULL;
    }

    return packet_alloc_and_tx(handle, p_reply, p_message, NULL, NULL);
}

uint32_t access_model_element_index_get(access_model_handle_t handle, uint16_t * p_element_index)
{
    if (p_element_index == NULL)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_element_index = m_model_pool[handle].model_info.element_index;
        return NRF_SUCCESS;
    }
}

/* ****** Internal API ****** */
uint32_t access_model_publish_address_set(access_model_handle_t handle, dsm_handle_t address_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (DSM_ADDR_MAX <= address_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (m_model_pool[handle].model_info.publish_address_handle == address_handle)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_model_pool[handle].model_info.publish_address_handle = address_handle;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_address_get(access_model_handle_t handle, dsm_handle_t * p_address_handle)
{
    if (NULL == p_address_handle)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_address_handle = m_model_pool[handle].model_info.publish_address_handle;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_retransmit_set(access_model_handle_t handle,
                                             access_publish_retransmit_t retransmit_params)
{
    if (ACCESS_MODEL_COUNT <= handle ||
        !ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[handle].internal_state))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_model_pool[handle].model_info.publication_retransmit.count == retransmit_params.count &&
             m_model_pool[handle].model_info.publication_retransmit.interval_steps == retransmit_params.interval_steps)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_model_pool[handle].model_info.publication_retransmit = retransmit_params;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_retransmit_get(access_model_handle_t handle,
                                             access_publish_retransmit_t * p_retransmit_params)
{
    if (p_retransmit_params == NULL)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_MODEL_COUNT <= handle ||
        !ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[handle].internal_state))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_retransmit_params = m_model_pool[handle].model_info.publication_retransmit;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_period_divisor_set(access_model_handle_t handle, uint16_t publish_divisor)
{

    if (ACCESS_MODEL_COUNT <= handle ||
        !ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[handle].internal_state))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_model_pool[handle].publication_state.publish_timeout_cb == NULL)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else if (publish_divisor == 0)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    m_model_pool[handle].publish_divisor = publish_divisor;
    access_publish_timing_update(handle);

    return NRF_SUCCESS;
}

uint32_t access_model_publish_period_set(access_model_handle_t handle,
                                         access_publish_resolution_t resolution,
                                         uint8_t step_number)
{
    if (ACCESS_MODEL_COUNT <= handle ||
        !ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[handle].internal_state))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_model_pool[handle].publication_state.publish_timeout_cb == NULL)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else if (step_number > ACCESS_PUBLISH_PERIOD_STEP_MAX ||
             resolution > ACCESS_PUBLISH_RESOLUTION_MAX) /*lint !e685 */
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (m_model_pool[handle].model_info.publication_period.step_num == step_number &&
             m_model_pool[handle].model_info.publication_period.step_res == resolution)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_model_pool[handle].model_info.publication_period.step_num = step_number;
        m_model_pool[handle].model_info.publication_period.step_res = resolution;
        access_publish_timing_update(handle);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_period_get(access_model_handle_t handle,
                                         access_publish_resolution_t * p_resolution,
                                         uint8_t * p_step_number)
{
    if (NULL == p_resolution || NULL == p_step_number)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_model_pool[handle].publication_state.publish_timeout_cb == NULL)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else
    {
        *p_resolution = (access_publish_resolution_t) m_model_pool[handle].model_info.publication_period.step_res;
        *p_step_number = m_model_pool[handle].model_info.publication_period.step_num;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_subscription_list_alloc(access_model_handle_t handle)
{
    if (m_access_model_config_frozen)
    {
        return NRF_ERROR_FORBIDDEN;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (ACCESS_SUBSCRIPTION_LIST_COUNT != m_model_pool[handle].model_info.subscription_pool_index)
    {
        return NRF_SUCCESS;
    }
    else
    {
        for (uint32_t i = 0; i < ACCESS_SUBSCRIPTION_LIST_COUNT; ++i)
        {
            if (!ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_subscription_list_pool[i].internal_state))
            {
                ACCESS_INTERNAL_STATE_ALLOCATED_SET(m_subscription_list_pool[i].internal_state);
                ACCESS_INTERNAL_STATE_OUTDATED_SET(m_subscription_list_pool[i].internal_state);
                m_model_pool[handle].model_info.subscription_pool_index = i;
                return NRF_SUCCESS;
            }
        }
        return NRF_ERROR_NO_MEM;
    }
}

uint32_t access_model_subscription_list_dealloc(access_model_handle_t handle)
{
    uint16_t sub_pool_index;

    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }

    sub_pool_index = m_model_pool[handle].model_info.subscription_pool_index;
    if (ACCESS_SUBSCRIPTION_LIST_COUNT == sub_pool_index)
    {
        return NRF_SUCCESS;
    }
    else if (m_access_model_config_frozen)
    {
        return NRF_ERROR_FORBIDDEN;
    }

    if (ACCESS_SUBSCRIPTION_LIST_COUNT > sub_pool_index)
    {
        if (!model_subscription_list_is_shared(handle))
        {
            /* This also has an effect of clearing `OUTDATED` flag */
            memset(&m_subscription_list_pool[sub_pool_index], 0, sizeof(m_subscription_list_pool[0]));
            /* @todo: subscription list de-allocation will change once mesh_config is integrated with access */
        }

        m_model_pool[handle].model_info.subscription_pool_index = ACCESS_SUBSCRIPTION_LIST_COUNT;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);

        return NRF_SUCCESS;
    }

    return NRF_ERROR_INVALID_STATE;
}

uint32_t access_model_subscription_lists_share(access_model_handle_t owner, access_model_handle_t other)
{
    if (m_access_model_config_frozen)
    {
        return NRF_ERROR_FORBIDDEN;
    }
    else if (!model_handle_valid_and_allocated(owner) || !model_handle_valid_and_allocated(other))
    {
        return NRF_ERROR_NOT_FOUND;
    }

    if (ACCESS_SUBSCRIPTION_LIST_COUNT > m_model_pool[owner].model_info.subscription_pool_index)
    {

        if (m_model_pool[other].model_info.subscription_pool_index == m_model_pool[owner].model_info.subscription_pool_index)
        {
            return NRF_SUCCESS;
        }

        if (ACCESS_SUBSCRIPTION_LIST_COUNT >= m_model_pool[other].model_info.subscription_pool_index)
        {
            uint32_t status = access_model_subscription_list_dealloc(other);
            if (status != NRF_SUCCESS)
            {
                return status;
            }

            m_model_pool[other].model_info.subscription_pool_index = m_model_pool[owner].model_info.subscription_pool_index;
            return NRF_SUCCESS;
        }
    }

    return NRF_ERROR_INVALID_STATE;
}

uint32_t access_model_subscription_add(access_model_handle_t handle, dsm_handle_t address_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (ACCESS_SUBSCRIPTION_LIST_COUNT  == m_model_pool[handle].model_info.subscription_pool_index)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else if (DSM_ADDR_MAX <= address_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (bitfield_get(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].bitfield, address_handle))
    {
        return NRF_SUCCESS;
    }
    else
    {
        bitfield_set(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].bitfield, address_handle);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_subscription_remove(access_model_handle_t handle, dsm_handle_t address_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (ACCESS_SUBSCRIPTION_LIST_COUNT == m_model_pool[handle].model_info.subscription_pool_index)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else if (DSM_ADDR_MAX <= address_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (!bitfield_get(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].bitfield, address_handle))
    {
        return NRF_SUCCESS;
    }
    else
    {
        bitfield_clear(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].bitfield, address_handle);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_subscription_list_pool[m_model_pool[handle].model_info.subscription_pool_index].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_subscriptions_get(access_model_handle_t handle,
                                        dsm_handle_t * p_address_handles,
                                        uint16_t * p_count)
{
    if (NULL == p_address_handles || NULL == p_count)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (ACCESS_SUBSCRIPTION_LIST_COUNT == m_model_pool[handle].model_info.subscription_pool_index)
    {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    else
    {
        const uint32_t max_count = *p_count;
        *p_count = 0;
        for (uint32_t i = 0; i < DSM_ADDR_MAX; ++i)
        {
            if (model_subscribes_to_addr(&m_model_pool[handle], i))
            {
                if (*p_count >= max_count)
                {
                    return NRF_ERROR_INVALID_LENGTH;
                }
                p_address_handles[*p_count] = i;
                (*p_count)++;
            }
        }
        return NRF_SUCCESS;
    }
}

uint32_t access_model_application_bind(access_model_handle_t handle, dsm_handle_t appkey_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (DSM_APP_MAX + DSM_DEVICE_MAX <= appkey_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (bitfield_get(m_model_pool[handle].model_info.application_keys_bitfield, appkey_handle))
    {
        return NRF_SUCCESS;
    }
    else
    {
        bitfield_set(m_model_pool[handle].model_info.application_keys_bitfield, appkey_handle);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_application_unbind(access_model_handle_t handle, dsm_handle_t appkey_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (DSM_APP_MAX + DSM_DEVICE_MAX <= appkey_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (!bitfield_get(m_model_pool[handle].model_info.application_keys_bitfield, appkey_handle))
    {
        return NRF_SUCCESS;
    }
    else
    {
        bitfield_clear(m_model_pool[handle].model_info.application_keys_bitfield, appkey_handle);
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_applications_get(access_model_handle_t handle,
                                       dsm_handle_t * p_appkey_handles,
                                       uint16_t * p_count)
{
    if (NULL == p_appkey_handles || NULL == p_count)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        const uint32_t max_count = *p_count;
        *p_count = 0;
        for (uint32_t i = 0; i < DSM_APP_MAX; ++i)
        {
            if (bitfield_get(m_model_pool[handle].model_info.application_keys_bitfield, i))
            {
                if (*p_count >= max_count)
                {
                    return NRF_ERROR_INVALID_LENGTH;
                }
                p_appkey_handles[*p_count] = i;
                (*p_count)++;
            }
        }
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_application_set(access_model_handle_t handle, dsm_handle_t appkey_handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (DSM_APP_MAX + DSM_DEVICE_MAX <= appkey_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (m_model_pool[handle].model_info.publish_appkey_handle == appkey_handle)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_model_pool[handle].model_info.publish_appkey_handle = appkey_handle;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_application_get(access_model_handle_t handle,
                                              dsm_handle_t * p_appkey_handle)
{
    if (NULL == p_appkey_handle)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_appkey_handle = m_model_pool[handle].model_info.publish_appkey_handle;
        return NRF_SUCCESS;
    }
}

uint32_t access_default_ttl_set(uint8_t ttl)
{
    if ((ttl > NRF_MESH_TTL_MAX) || (ttl == 1))
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else
    {
        m_default_ttl = ttl;
        return NRF_SUCCESS;
    }
}

uint8_t access_default_ttl_get(void)
{
    return m_default_ttl;
}

uint32_t access_model_publish_friendship_credential_flag_set(access_model_handle_t handle, bool flag)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_model_pool[handle].model_info.friendship_credential_flag != flag)
    {
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
    }

    m_model_pool[handle].model_info.friendship_credential_flag = flag;
    return NRF_SUCCESS;
}

uint32_t access_model_publish_friendship_credential_flag_get(access_model_handle_t handle, bool * p_flag)
{
    if (p_flag == NULL)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_flag = m_model_pool[handle].model_info.friendship_credential_flag;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_ttl_set(access_model_handle_t handle, uint8_t ttl)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (NRF_MESH_TTL_MAX < ttl && ttl != ACCESS_TTL_USE_DEFAULT)
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    else if (m_model_pool[handle].model_info.publish_ttl == ttl)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_model_pool[handle].model_info.publish_ttl = ttl;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publish_ttl_get(access_model_handle_t handle, uint8_t * p_ttl)
{
    if (NULL == p_ttl)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_ttl = m_model_pool[handle].model_info.publish_ttl;
        return NRF_SUCCESS;
    }
}

uint32_t access_element_location_set(uint16_t element_index, uint16_t location)
{
    if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else if (m_element_pool[element_index].location == location)
    {
        return NRF_SUCCESS;
    }
    else
    {
        m_element_pool[element_index].location = location;
        ACCESS_INTERNAL_STATE_OUTDATED_SET(m_element_pool[element_index].internal_state);
        return NRF_SUCCESS;
    }
}

uint32_t access_element_location_get(uint16_t element_index, uint16_t * p_location)
{
    if (NULL == p_location)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_location = m_element_pool[element_index].location;
        return NRF_SUCCESS;
    }
}

uint32_t access_element_sig_model_count_get(uint16_t element_index, uint8_t * p_sig_model_count)
{
    if (NULL == p_sig_model_count)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_sig_model_count = m_element_pool[element_index].sig_model_count;
        return NRF_SUCCESS;
    }
}

uint32_t access_element_vendor_model_count_get(uint16_t element_index, uint8_t * p_vendor_model_count)
{
    if (NULL == p_vendor_model_count)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_vendor_model_count = m_element_pool[element_index].vendor_model_count;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_id_get(access_model_handle_t handle, access_model_id_t * p_model_id)
{
    if (NULL == p_model_id)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        p_model_id->model_id = m_model_pool[handle].model_info.model_id.model_id;
        p_model_id->company_id = m_model_pool[handle].model_info.model_id.company_id;
        return NRF_SUCCESS;
    }
}

uint32_t access_handle_get(uint16_t element_index,
                           access_model_id_t model_id,
                           access_model_handle_t * p_handle)
{
    if (p_handle == NULL)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *p_handle = ACCESS_HANDLE_INVALID;
        for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
        {
            if (m_model_pool[i].model_info.element_index == element_index &&
                m_model_pool[i].model_info.model_id.model_id == model_id.model_id &&
                m_model_pool[i].model_info.model_id.company_id == model_id.company_id)
            {
                *p_handle = i;
                return NRF_SUCCESS;
            }
        }
        /* Couldn't find the model. */
        return NRF_ERROR_NOT_FOUND;
    }
}

uint32_t access_element_models_get(uint16_t element_index, access_model_handle_t * p_models, uint16_t * p_count)
{
    if (NULL == p_models || p_count == NULL)
    {
        return NRF_ERROR_NULL;
    }
    else if (ACCESS_ELEMENT_COUNT <= element_index)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        const uint16_t max_count = *p_count;
        *p_count = 0;
        for (uint32_t i = 0; i < ACCESS_MODEL_COUNT; ++i)
        {
            if (*p_count >= max_count)
            {
                return NRF_ERROR_INVALID_LENGTH;
            }
            else if ((ACCESS_INTERNAL_STATE_IS_ALLOCATED(m_model_pool[i].internal_state)) &&
                     m_model_pool[i].model_info.element_index == element_index)
            {
                p_models[*p_count] = i;
                (*p_count)++;
            }
        }
        return NRF_SUCCESS;
    }
}

uint32_t access_model_p_args_get(access_model_handle_t handle, void ** pp_args)
{
    if (NULL == pp_args)
    {
        return NRF_ERROR_NULL;
    }
    else if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        *pp_args = m_model_pool[handle].p_args;
        return NRF_SUCCESS;
    }
}

uint32_t access_model_publication_stop(access_model_handle_t handle)
{
    if (!model_handle_valid_and_allocated(handle))
    {
        return NRF_ERROR_NOT_FOUND;
    }

    access_publish_period_set(&m_model_pool[handle].publication_state, (access_publish_resolution_t)0, 0);
    (void) access_model_reliable_cancel(handle);
    (void) dsm_address_publish_remove(m_model_pool[handle].model_info.publish_address_handle);
    m_model_pool[handle].model_info.publish_address_handle = DSM_HANDLE_INVALID;
    m_model_pool[handle].model_info.publish_appkey_handle = DSM_HANDLE_INVALID;
    m_model_pool[handle].model_info.publication_retransmit.count = 0;
    m_model_pool[handle].model_info.publication_retransmit.interval_steps = 0;
    m_model_pool[handle].model_info.publish_ttl = 0;
    m_model_pool[handle].model_info.publication_period.step_num = 0;
    m_model_pool[handle].model_info.publication_period.step_res = 0;
    m_model_pool[handle].publication_state.period.step_num = 0;
    m_model_pool[handle].publication_state.period.step_res = 0;
    ACCESS_INTERNAL_STATE_OUTDATED_SET(m_model_pool[handle].internal_state);

    return NRF_SUCCESS;
}

uint32_t access_model_publication_by_appkey_stop(dsm_handle_t appkey_handle)
{
    dsm_handle_t local_handle;

    if (DSM_APP_MAX + DSM_DEVICE_MAX <= appkey_handle)
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    for (access_model_handle_t i = 0; i < ACCESS_MODEL_COUNT; i++)
    {
        if (NRF_SUCCESS == access_model_publish_application_get(i, &local_handle))
        {
            if (local_handle == appkey_handle)
            {
                (void) access_model_publication_stop(i);
            }
        }
    }

    return NRF_SUCCESS;
}

const void * access_flash_area_get(void)
{
#ifdef ACCESS_FLASH_AREA_LOCATION
    return (const flash_manager_page_t *) ACCESS_FLASH_AREA_LOCATION;
#else
    return (const flash_manager_page_t *) (((const uint8_t *) dsm_flash_area_get()) - (ACCESS_FLASH_PAGE_COUNT * PAGE_SIZE));
#endif
}
