#ifndef TEST_MODEL_H__
#define TEST_MODEL_H__

#include <stdint.h>
#include "access.h"
#include "access_reliable.h"
#include "generic_onoff_common.h"
#include "generic_onoff_messages.h"

#include "test_data_type.h"

/**
 * @defgroup GENERIC_ONOFF_CLIENT Generic OnOff client model interface
 * @ingroup GENERIC_ONOFF_MODEL
 * @{
 */

/** Client model ID */
#define CUSTOM_CLIENT_MODEL_ID 0x1001

/* Forward declaration */
typedef struct __custom_client_t custom_client_t;

/**
 * Callback type for OnOff state related transactions
 *
 * @param[in]     p_self                   Pointer to the model structure
 * @param[in]     p_meta                   Access metadata for the received message
 * @param[in]     p_in                     Pointer to the input event parameters for the user application
 */
typedef void (*custom_state_status_cb_t)(const custom_client_t * p_self,
                                                const access_message_rx_meta_t * p_meta,
                                                const custom_status_params_t * p_in);
typedef struct
{
    /** Client model response message callback. */
    custom_state_status_cb_t onoff_status_cb;

    /** Callback to call after the acknowledged transaction has ended. */
    access_reliable_cb_t ack_transaction_status_cb;
    /** callback called at the end of the each period for the publishing */
    access_publish_timeout_cb_t periodic_publish_cb;
} custom_client_callbacks_t;

/**
 * User provided settings and callbacks for the model instance
 */
typedef struct
{
    /** Reliable message timeout in microseconds. If this value is set to zero, during model
     * initialization this value will be updated to the value specified by
     * by @ref MODEL_ACKNOWLEDGED_TRANSACTION_TIMEOUT. */
    uint32_t timeout;
    /** If server should force outgoing messages as segmented messages */
    bool force_segmented;
    /** TransMIC size used by the outgoing server messages. See @ref nrf_mesh_transmic_size_t */
    nrf_mesh_transmic_size_t transmic_size;

    /** Callback list */
    const custom_client_callbacks_t * p_callbacks;
} custom_client_settings_t;

/** Union for holding current message packet */
typedef union
{
    custom_set_msg_pkt_t set;
} custom_client_msg_data_t;

/**  */
struct __custom_client_t
{
    /** Model handle assigned to this instance */
    access_model_handle_t model_handle;
    /** Holds the raw message packet data for transactions */
    custom_client_msg_data_t msg_pkt;
    /* Acknowledged message context variable */
    access_reliable_t access_message;

    /** Model settings and callbacks for this instance */
    custom_client_settings_t settings;
};

/**
 * Initializes Generic On Off client.
 *
 * @note This function should only be called _once_.
 * @note The client handles the model allocation and adding.
 *
 * @param[in]     p_client                 Client model context pointer.
 * @param[in]     element_index            Element index to add the model
 *
 * @retval   NRF_SUCCESS    If model is initialized succesfully
 * @returns  Other appropriate error codes on failure.
 */
uint32_t custom_client_init(custom_client_t * p_client, uint8_t element_index);

/**
 * Sends a Set message to the server.
 *
 * @note Expected response: Status, if the message is sent as acknowledged message.
 *
 * @param[in]     p_client                 Client model context pointer.
 * @param[in]     p_params                 Message parameters.
 * @param[in]     p_transition             Optional transition parameters
 *
 * @retval   NRF_SUCCESS    If the message is handed over to the mesh stack for transmission.
 * @returns  Other appropriate error codes on failure.
 */
uint32_t custom_client_set(custom_client_t * p_client, const custom_set_params_t * p_params,
                                  const model_transition_t * p_transition);

/**
 * Sends a Set Unacknowledged message to the server.
 *
 * @note Expected response: Status, if the message is sent as acknowledged message.
 *
 * @param[in]     p_client                 Client model context pointer.
 * @param[in]     p_params                 Message parameters.
 * @param[in]     p_transition             Optional transition parameters
 * @param[in]     repeats                  Number of repetitions to use while sending unacknowledged message.
 *
 * @retval   NRF_SUCCESS    If the message is handed over to the mesh stack for transmission.
 * @returns  Other appropriate error codes on failure.
 */
uint32_t custom_client_set_unack(custom_client_t * p_client, const custom_set_params_t * p_params,
                                        uint8_t repeats);

/**
 * Sends a Get message to the server.
 *
 * @note Expected response: Status
 *
 * @param[in]     p_client                 Client model context pointer.
 *
 * @retval   NRF_SUCCESS    If the message is handed over to the mesh stack for transmission.
 * @returns  Other appropriate error codes on failure.
 */
uint32_t custom_client_get(custom_client_t * p_client);

/**@} end of GENERIC_ONOFF_CLIENT */


#endif /* _TEST_MODEL_H__ */