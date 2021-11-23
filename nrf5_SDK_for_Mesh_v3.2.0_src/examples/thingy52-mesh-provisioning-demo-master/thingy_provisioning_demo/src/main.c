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
 *    engineered, decompiled, modified and/or disassembled.F
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

#include <stdint.h>
#include <string.h>

/* HAL */
#include "boards.h"
#include "simple_hal_thingy.h"
#include "app_timer.h"

/* Core */
#include "nrf_mesh_config_core.h"
#include "nrf_mesh_gatt.h"
#include "nrf_mesh_configure.h"
#include "nrf_mesh.h"
#include "mesh_stack.h"
#include "device_state_manager.h"
#include "access_config.h"
#include "proxy.h"
#include "m_ui.h"
/* Provisioning and configuration */
#include "my_mesh_provisionee.h"
#include "mesh_app_utils.h"

/* Models */
#include "generic_onoff_server.h"
#include "generic_onoff_client.h"

/* Logging and RTT */
#include "log.h"
#include "rtt_input.h"

/* Example specific includes */
#include "app_config.h"
#include "example_common.h"
#include "nrf_mesh_config_examples.h"
#include "light_switch_example_common.h"
#include "app_onoff.h"
#include "ble_softdevice_support.h"
#include "pca20020.h"
#include "drv_ext_light.h"
#include "drv_ext_gpio.h"
#include "nrf_delay.h"

// custom include
#include "drv_humidity.h"
#include "drv_pressure.h"
#include "drv_hts221.h"
//#include "drv_lps22hb.h"
#include "sensor_manage.h"
#include "nrf_drv_gpiote.h"
#include "test_model.h"
#include "test_model_server.h"
#include "test_data_type.h"
#include "device_manager.h"
#include "device_addr_manager.h"
#include "hal.h"

#define ONOFF_SERVER_0_LED          (BSP_LED_0)
#define APP_ONOFF_ELEMENT_INDEX     (0)
#define THINGY_BUTTON 11
#define USBCHARGE_PIN 13
#define BUTTON_PIN_CONFIG ((GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos)     | \
                           (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)    | \
                           (BUTTON_PULL << GPIO_PIN_CNF_PULL_Pos)                 | \
                           (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | \
                           (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos))
#define USBCHARGE_PIN_CONFIG_HIGH ((GPIO_PIN_CNF_SENSE_High << GPIO_PIN_CNF_SENSE_Pos)     | \
                           (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)    | \
                           (NRF_GPIO_PIN_PULLDOWN << GPIO_PIN_CNF_PULL_Pos)                 | \
                           (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | \
                           (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos))
#define USBCHARGE_PIN_CONFIG_LOW ((GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos)     | \
                           (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)    | \
                           (NRF_GPIO_PIN_PULLDOWN << GPIO_PIN_CNF_PULL_Pos)                 | \
                           (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | \
                           (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos))
#define APP_UNACK_MSG_REPEAT_COUNT   (2)
#define GPIOTE_IRQ_LEVEL NRF_MESH_IRQ_PRIORITY_LOWEST   
static uint32_t m_last_button_press;
static const nrf_drv_twi_t     m_twi_sensors = NRF_DRV_TWI_INSTANCE(TWI_SENSOR_INSTANCE);
static bool m_device_provisioned;
uint32_t hal_buttons_init(void);
static bool m_on_off_button_flag= 0;
/*************************************************************************************************/
static void app_custom_server_set_cb(const app_onoff_server_t * p_server, app_onoff_state_t * send_data);
static void app_custom_server_get_cb(const app_onoff_server_t * p_server, app_onoff_state_t * send_data);
static custom_client_t m_client;
static custom_client_t c_client;
static uint8_t USB_connected=0;
/* Generic OnOff server structure definition and initialization */
APP_ONOFF_SERVER_DEF(m_onoff_server_0,
                     APP_CONFIG_FORCE_SEGMENTATION,
                     APP_CONFIG_MIC_SIZE,
                     app_custom_server_set_cb,
                     app_custom_server_get_cb)


static void app_custom_client_publish_interval_cb(access_model_handle_t handle, void * p_self);
static void app_custom_client_status_cb(const custom_client_t * p_self,
                                               const access_message_rx_meta_t * p_meta,
                                               const custom_status_params_t * p_in);
static void app_custom_client_transaction_status_cb(access_model_handle_t model_handle,
                                                       void * p_args,
                                                       access_reliable_status_t status);

const custom_client_callbacks_t custom_client_cbs =
{
    .onoff_status_cb = app_custom_client_status_cb,
    .ack_transaction_status_cb = app_custom_client_transaction_status_cb,
    .periodic_publish_cb = app_custom_client_publish_interval_cb
};

ble_tes_temperature_t t_conv_data = 
{
  .integer = -1,
  .decimal = -1
};
ble_tes_humidity_t h_conv_data = -1;
ble_tes_pressure_t p_conv_data = 
{
  .integer = -1,
  .decimal = -1
};


static app_onoff_state_t save_data;
static void mesh_send_manager(custom_set_params_t send_data);

static void send_inst_requset(inst_packet_list_t inst, uint8_t *addr)
{
    custom_set_params_t set_params;
    set_params.temp_intager = -50;
    set_params.temp_decimal = 0;
    set_params.humi = 100 + inst;
    set_params.ttl = 10;
    set_params.tid = (uint8_t)get_millis();
    strcpy(set_params.addr, addr);
    //set_params.addr = addr;
    mesh_send_manager(set_params);
}

/* Callback for updating the hardware state */
static void app_custom_server_set_cb(const app_onoff_server_t * p_server, app_onoff_state_t *send_data)
{
    /* Resolve the server instance here if required, this example uses only 1 instance. */
    
    save_data.temp_intager = send_data->temp_intager;
    save_data.temp_decimal = send_data->temp_decimal;
    save_data.humi = send_data->humi;

    
    #if IS_CLIENT

     __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "t_data = %2d.%-2d h_data = %2d ttl = %d\n", 
                                        send_data->temp_intager, send_data->temp_decimal, send_data->humi, send_data->ttl);

      custom_set_params_t set_parm = 
      {
        .temp_intager = send_data->temp_intager,
        .temp_decimal = send_data->temp_decimal,
        .humi = send_data->humi,
        .ttl = send_data->ttl,
        .tid = send_data->tid,
        //.addr = send_data->addr
      };

      strcpy(set_parm.addr, send_data->addr);

      mesh_send_manager(set_parm);
    #else

     //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "get_data! t_data = %2d.%-2d h_data = %2d ttl = %d\n", 
     //                                   send_data->temp_intager, send_data->temp_decimal, send_data->humi, send_data->ttl);

    #endif
    
}

/* Callback for reading the hardware state */
static void app_custom_server_get_cb(const app_onoff_server_t * p_server, app_onoff_state_t * send_data)
{
    /* Resolve the server instance here if required, this example uses only 1 instance. */
    
    send_data->temp_intager = save_data.temp_intager;
    send_data->temp_decimal = save_data.temp_decimal;
    send_data->humi = save_data.humi;
    send_data->ttl = save_data.ttl;

    //*p_present_onoff = hal_led_pin_get();
}

static void app_model_init(void)
{
    /* Instantiate onoff server on element index APP_ONOFF_ELEMENT_INDEX */
    ERROR_CHECK(app_onoff_init(&m_onoff_server_0, APP_ONOFF_ELEMENT_INDEX+1));
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "App OnOff Model Handle: %d\n", m_onoff_server_0.server.model_handle);
}

/* This callback is called periodically if model is configured for periodic publishing */
static void app_custom_client_publish_interval_cb(access_model_handle_t handle, void * p_self)
{
     __LOG(LOG_SRC_APP, LOG_LEVEL_WARN, "Publish desired message here.\n");
}

/* Acknowledged transaction status callback, if acknowledged transfer fails, application can
* determine suitable course of action (e.g. re-initiate previous transaction) by using this
* callback.
*/
static void app_custom_client_transaction_status_cb(access_model_handle_t model_handle,
                                                       void * p_args,
                                                       access_reliable_status_t status)
{
    switch(status)
    {
        case ACCESS_RELIABLE_TRANSFER_SUCCESS:
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Acknowledged transfer success.\n");
            break;

        case ACCESS_RELIABLE_TRANSFER_TIMEOUT:
            
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Acknowledged transfer timeout.\n");
            break;

        case ACCESS_RELIABLE_TRANSFER_CANCELLED:
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Acknowledged transfer cancelled.\n");
            break;

        default:
            ERROR_CHECK(NRF_ERROR_INTERNAL);
            break;
    }
}

/* Generic OnOff client model interface: Process the received status message in this callback */
static void app_custom_client_status_cb(const custom_client_t * p_self,
                                               const access_message_rx_meta_t * p_meta,
                                               const custom_status_params_t * p_in)
{
    //if (p_in->remaining_time_ms > 0)
    //{
    //    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "OnOff server: 0x%04x, Present OnOff: %d, Target OnOff: %d, Remaining Time: %d ms\n",
    //          p_meta->src.value, p_in->present_on_off, p_in->target_on_off, p_in->remaining_time_ms);
    //}
    //else
    //{
    //    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "OnOff server: 0x%04x, Present OnOff: %d\n",
    //          p_meta->src.value, p_in->present_on_off);
    //}
}
/*************************************************************************************************/


static void node_reset(void)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "----- Node reset  -----\n");
    hal_led_blink_ms(LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_RESET);
    /* This function may return if there are ongoing flash operations. */
    mesh_stack_device_reset();
}

static void config_server_evt_cb(const config_server_evt_t * p_evt)
{
    if (p_evt->type == CONFIG_SERVER_EVT_NODE_RESET)
    {
        node_reset();
    }
}

bool toggle = true;
bool test_toggle = false;

static void button_event_handler(uint32_t button_number)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Button %d pressed\n", button_number);

    custom_set_params_t set_params;

    switch (button_number)
    {
        /* Pressing SW1 on the Development Kit will result in LED state to toggle and trigger
        the STATUS message to inform client about the state change. This is a demonstration of
        state change publication due to local event. */
        case 0:
        case 1:
        {
            #if IS_CLIENT
            set_params.temp_intager = t_conv_data.integer;
            set_params.temp_decimal = t_conv_data.decimal;
            set_params.humi = h_conv_data;
            set_params.ttl = 10;
            set_params.tid = (uint8_t)get_millis();
            strcpy(set_params.addr, get_this_addr());
            //set_params.addr = get_this_addr();
            mesh_send_manager(set_params); 
            //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "a = %d %d\n",get_available(0), get_available(1));
            //print_all();

            #else
            
            if(toggle)
            {
                send_inst_requset(SEND_ADDR, get_this_addr());
                toggle = false;
                test_toggle = true;
            }
            else
            {
                get_data_all();

            }

            #endif
        }
        default:
            break;
    }
}

void mesh_send_manager(custom_set_params_t send_data) // ----------------------------------
{
    uint32_t status = NRF_SUCCESS;
    
    
    uint32_t err = custom_client_set_unack(&c_client, &send_data, 3);

    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "send = %d\n",err);
}

static void app_rtt_input_handler(int key)
{
    if (key >= '0' && key <= '4')
    {
        uint32_t button_number = key - '0';
        button_event_handler(button_number);
    }
}

static void device_identification_start_cb(uint8_t attention_duration_s)
{
    hal_led_pin_set(false);
    hal_led_blink_ms(LED_BLINK_ATTENTION_INTERVAL_MS,
                     LED_BLINK_ATTENTION_COUNT(3));

}

static void provisioning_aborted_cb(void)
{
    hal_led_blink_stop();
}

static void provisioning_complete_cb(void)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Successfully provisioned\n");

#if MESH_FEATURE_GATT_ENABLED
    /* Restores the application parameters after switching from the Provisioning
     * service to the Proxy  */
    gap_params_init();
    conn_params_init();
#endif

    dsm_local_unicast_address_t node_address;
    dsm_local_unicast_addresses_get(&node_address);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Node Address: 0x%04x \n", node_address.address_start);
    hal_led_blink_stop();
    hal_led_pin_set(0);
    hal_led_blink_ms(LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_PROV);
}

static void models_init_cb(void)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Initializing and adding models\n");
    app_model_init();

    m_client.settings.p_callbacks = &custom_client_cbs;
    m_client.settings.timeout = 0;
    m_client.settings.force_segmented = APP_CONFIG_FORCE_SEGMENTATION;
    m_client.settings.transmic_size = APP_CONFIG_MIC_SIZE;

    c_client.settings.p_callbacks = &custom_client_cbs;
    c_client.settings.timeout = 0;
    c_client.settings.force_segmented = APP_CONFIG_FORCE_SEGMENTATION;
    c_client.settings.transmic_size = APP_CONFIG_MIC_SIZE;

    //ERROR_CHECK(generic_onoff_client_init(&m_client,  APP_ONOFF_ELEMENT_INDEX+1));
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", custom_client_init(&c_client,  APP_ONOFF_ELEMENT_INDEX+1));
    //ERROR_CHECK(custom_client_init(&c_client,  APP_ONOFF_ELEMENT_INDEX+2));
}
static void board_init(void)
{
    uint32_t            err_code;
    drv_ext_gpio_init_t ext_gpio_init;


    static const nrf_drv_twi_config_t twi_config =
    {
        .scl                = TWI_SCL,
        .sda                = TWI_SDA,
        .frequency          = NRF_TWI_FREQ_400K,
        .interrupt_priority = APP_IRQ_PRIORITY_LOW
    };

    static const drv_sx1509_cfg_t sx1509_cfg =
    {
        .twi_addr       = SX1509_ADDR,
        .p_twi_instance = &m_twi_sensors,
        .p_twi_cfg      = &twi_config
    };

    ext_gpio_init.p_cfg = &sx1509_cfg;
    
    err_code = support_func_configure_io_startup(&ext_gpio_init);
    APP_ERROR_CHECK(err_code);
}

/**************************************************************************
/* Handle GPIO interrupt, either button press or USB cable connected
/**************************************************************************/

void GPIOTE_IRQHandler(void)
{
    NRF_GPIOTE->EVENTS_PORT = 0;
   
        /* Check that the event was generated by a button press, and reject if it's too soon (debounce).
         * NOTE: There is a bug with this at the wrap-around for the RTC0 where the button could be
         * pressed before HAL_BUTTON_PRESS_FREQUENCY has passed a single time. It doesn't matter practically.
         */

  //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "event\n");
  if (USB_connected!=nrf_gpio_pin_read(USBCHARGE_PIN))
   {
       USB_connected = nrf_gpio_pin_read(USBCHARGE_PIN);
       if (USB_connected)
       {
        //  hal_led_blink_ms(50,2);
          NRF_GPIO->PIN_CNF[USBCHARGE_PIN] = USBCHARGE_PIN_CONFIG_LOW;
          if (!hal_led_pin_get()) 
          {
              led_breath_yellow();
          }
          else
          {
              charging_indicate_start();
           }
       }
       else
       {
       //    hal_led_blink_ms(50,2);
          NRF_GPIO->PIN_CNF[USBCHARGE_PIN] = USBCHARGE_PIN_CONFIG_HIGH;
          charging_indicate_stop();
          //if LED is off, maybe we are breathing yellow, stop breathing
          if(!hal_led_pin_get())hal_led_pin_set(0);
       }

    }
    else
    {
          
       if( TIMER_DIFF(m_last_button_press, NRF_RTC0->COUNTER) > HAL_BUTTON_PRESS_FREQUENCY)
        {
          //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Buttonpressed\n");
            m_last_button_press = NRF_RTC0->COUNTER;
            button_event_handler(0);
        }
        if(USB_connected)charging_indicate_start();
      }

    
}

uint32_t m_my_ui_init( void)
{
    uint32_t                        err_code;
    static drv_sx1509_cfg_t         sx1509_cfg;
    drv_ext_light_init_t            led_init;
    //lint --e{651} Potentially confusing initializer
    static const drv_ext_light_conf_t led_conf[DRV_EXT_LIGHT_NUM] = DRV_EXT_LIGHT_CFG;

    static const nrf_drv_twi_config_t twi_config =
    {
        .scl                = TWI_SCL,
        .sda                = TWI_SDA,
        .frequency          = NRF_TWI_FREQ_100K,
        .interrupt_priority = APP_IRQ_PRIORITY_LOW
    };
    sx1509_cfg.twi_addr       = SX1509_ADDR;
    sx1509_cfg.p_twi_instance = &m_twi_sensors;
    sx1509_cfg.p_twi_cfg      = &twi_config;



    led_init.p_light_conf        = led_conf;
    led_init.num_lights          = DRV_EXT_LIGHT_NUM;
    led_init.clkx_div            = DRV_EXT_LIGHT_CLKX_DIV_8;
    led_init.p_twi_conf          = &sx1509_cfg;
    led_init.resync_pin          = SX_RESET;

    err_code = drv_ext_light_init(&led_init, false);
    APP_ERROR_CHECK(err_code);
    ERROR_CHECK(drv_ext_light_off(DRV_EXT_RGB_LED_SENSE));
    ERROR_CHECK(drv_ext_light_off(DRV_EXT_RGB_LED_LIGHTWELL));
    nrf_gpio_cfg_output(MOS_1);
    nrf_gpio_cfg_output(MOS_2);
    nrf_gpio_cfg_output(MOS_3);
    nrf_gpio_cfg_output(MOS_4);
    nrf_gpio_pin_clear(MOS_1);
    nrf_gpio_pin_clear(MOS_2);
    nrf_gpio_pin_clear(MOS_3);
    nrf_gpio_pin_clear(MOS_4);
    //Button and charge detect pin. 
   NRF_GPIO->PIN_CNF[THINGY_BUTTON] = BUTTON_PIN_CONFIG;
    NRF_GPIO->PIN_CNF[USBCHARGE_PIN] = USBCHARGE_PIN_CONFIG_HIGH;
  
    NRF_GPIOTE->INTENSET = GPIOTE_INTENSET_PORT_Msk;
    NRF_GPIOTE->EVENTS_PORT  = 0;

    NVIC_SetPriority(GPIOTE_IRQn, GPIOTE_IRQ_LEVEL);
    NVIC_EnableIRQ(GPIOTE_IRQn);

    USB_connected= nrf_gpio_pin_read(USBCHARGE_PIN);
   // if (USB_connected) led_breath_yellow();
    return NRF_SUCCESS;
}
static void provisioning_blink_output_cb(uint8_t * number)

{   uint32_t err_code;
    ERROR_CHECK(drv_ext_light_off(1));
     __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "Blink OOB %u\n", number[15]);
     //The OOB data only use last byte to set the number of blink
     //Start the blinking timer
    hal_led_blink_stop();
    hal_led_pin_set(0);
    nrf_delay_ms(300);
    hal_led_blink_ms(500,number[15]);
    APP_ERROR_CHECK(err_code);
}
static void mesh_init(void)
{
    mesh_stack_init_params_t init_params =
    {
        .core.irq_priority       = NRF_MESH_IRQ_PRIORITY_LOWEST,
        .core.lfclksrc           = DEV_BOARD_LF_CLK_CFG,
        .core.p_uuid             = NULL,
        .models.models_init_cb   = models_init_cb,
        .models.config_server_cb = config_server_evt_cb
    };
    ERROR_CHECK(mesh_stack_init(&init_params, &m_device_provisioned));
}

static void initialize(void)
{
    __LOG_INIT(LOG_SRC_APP | LOG_SRC_FRIEND, LOG_LEVEL_DBG1, LOG_CALLBACK_DEFAULT);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "----- Thingy Provisioning Demo -----\n");
   
    ERROR_CHECK(app_timer_init());
    hal_leds_init();
    ble_stack_init();
#if MESH_FEATURE_GATT_ENABLED
    gap_params_init();
    conn_params_init();
#endif
    board_init();
    mesh_init();
    m_my_ui_init();

    
}

static void start(void)
{
    rtt_input_enable(app_rtt_input_handler, RTT_INPUT_POLL_PERIOD_MS);
    //nrf_gpio_cfg_input(THINGY_BUTTON,GPIO_PIN_CNF_PULL_Pullup);
    if (!m_device_provisioned)
    {
        static const uint8_t static_auth_data[NRF_MESH_KEY_SIZE] = STATIC_AUTH_DATA;
        mesh_provisionee_start_params_t prov_start_params =
        {
            .p_static_data    = static_auth_data,
            .prov_complete_cb = provisioning_complete_cb,
            .prov_device_identification_start_cb = device_identification_start_cb,
            .prov_device_identification_stop_cb = NULL,
            .auth_output_cb = NULL,//provisioning_blink_output_cb,  // �� ���ڿ� ���� ������         obb�� �����ǰ� �ȴ�
            .prov_abort_cb = provisioning_aborted_cb,
            .p_device_uri = EX_URI_LS_SERVER
        };
        ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));
        if(!USB_connected) 
        {
            led_breath_red();
        }

    }
    else
    {  
        //erase bond information if button pressed when reset
        if (nrf_gpio_pin_read(THINGY_BUTTON)==0)
        {
 
#if MESH_FEATURE_GATT_PROXY_ENABLED
            (void) proxy_stop();
#endif
            mesh_stack_config_clear();
            nrf_delay_ms(500);
            node_reset();
        }
   
        
     if(!USB_connected)
     {
     
        hal_led_blink_ms(100,2);
     }
        
    }
   
    mesh_app_uuid_print(nrf_mesh_configure_device_uuid_get());

    ERROR_CHECK(mesh_stack_start());

   /* hal_led_pin_set(0);
    hal_led_blink_ms(LEDS_MASK, LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_START);*/
}

static void drv_humidity_evt_handler(drv_humidity_evt_t event)
{
}

static void drv_pressure_evt_handler(drv_pressure_evt_t const * p_event)
{
}

static void init_sensor()
{
    uint32_t err_code;

    static const nrf_drv_twi_config_t twi_config =
    {
            .scl                = TWI_SCL,
            .sda                = TWI_SDA,
            .frequency          = NRF_TWI_FREQ_400K,
            .interrupt_priority = APP_IRQ_PRIORITY_LOW
    };

    static const drv_hts221_twi_cfg_t test_humi = 
    {
            .twi_addr = HTS221_ADDR,
            .pin_int = HTS_INT,
            .p_twi_instance = &m_twi_sensors,
            .p_twi_config = &twi_config
    };

    static const drv_hts221_cfg_t cfg = {
            .av_conf   = AV_CONF_REG_DEFAULT,
            .ctrl_reg1 = (CTRL_REG1_DEFAULT | (CTRL_REG1_PD_Active << CTRL_REG1_PD_Pos)),
            .ctrl_reg2 =  CTRL_REG2_DEFAULT,
            .ctrl_reg3 = (CTRL_REG3_DEFAULT | (CTRL_REG3_DRDY_Enable << CTRL_REG3_DRDY_Pos) | (CTRL_REG3_DRDY_H_L_ActiveLow << CTRL_REG3_DRDY_H_L_Pos) )
    };
          
    static drv_humidity_init_t    h_init_params =
    {
        .twi_addr            = HTS221_ADDR,
        .pin_int             = HTS_INT,
        .p_twi_instance      = &m_twi_sensors,
        .p_twi_cfg           = &twi_config,
        .evt_handler         = drv_humidity_evt_handler
    };
    
    static drv_pressure_init_t p_init_params =
    {
        .twi_addr            = LPS22HB_ADDR,
        .pin_int             = LPS_INT,
        .p_twi_instance      = &m_twi_sensors,
        .p_twi_cfg           = &twi_config,
        .evt_handler         = drv_pressure_evt_handler,
        .mode                = DRV_PRESSURE_MODE_BAROMETER
    };

    err_code = drv_humidity_init(&h_init_params);
    if(err_code != NRF_SUCCESS) while(1);

    err_code = drv_hts221_open(&test_humi);
    if(err_code != NRF_SUCCESS) while(1);
    err_code = drv_hts221_cfg_set(&cfg);
    if(err_code != NRF_SUCCESS) while(1);
    err_code = drv_hts221_close();
    if(err_code != NRF_SUCCESS) while(1);

    err_code = drv_pressure_init(&p_init_params);
    if(err_code != NRF_SUCCESS) while(1);
}

static bool sensor_start = false;
static bool init_toggle = true;

int main(void)
{
    initialize();
    start();
    
    ble_gap_addr_t ble_addr;
    sd_ble_gap_addr_get(&ble_addr);

    add_addr_node(ble_addr.addr);
    
    set_available(true, 0);
    print_addr(ble_addr.addr);

    init_device_manager(&t_conv_data, &h_conv_data, &sensor_start);
    init_send_manager(&mesh_send_manager);

    
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "init end!\n");
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", CUSTOM_SET_MAXLEN);


    unsigned long p_millis = 0;
    unsigned long test_p_millis = 0;
    uint8_t state = 0;

    bool toggle = false;

    for (;;)
    {
        (void)sd_app_evt_wait();
        
        #if IS_CLIENT
        if(sensor_start)
        {
            if(init_toggle)
            {
                init_toggle = false;
                init_sensor();
            }
        }
        
        if(get_millis() - p_millis >= 200 && sensor_start)
        {
          p_millis = get_millis();

          drv_humidity_sample();
          drv_pressure_sample();

          temperature_conv_data(drv_humidity_temp_get(), &t_conv_data);
          humidity_conv_data(drv_humidity_get(), &h_conv_data);
          pressure_conv_data(drv_pressure_get(), &p_conv_data);
          //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "t_data = %2d.%-2d h_data = %2d p_data = %4d.%-2d\n", t_conv_data.integer, t_conv_data.decimal, h_conv_data, p_conv_data.integer, p_conv_data.decimal);
        }
        #else
        //if(millis - p_millis >= 1000)
        //{
        //  p_millis = millis;
        //  if(toggle == false) toggle = true;
        //  else toggle = false;
        //}

        //if(toggle)
        //{
        //  send_data_requset();
        //}
        #endif
        if(test_toggle == true)
        {
            if(get_millis() - test_p_millis >= 100)
            {
                test_p_millis = get_millis();
                //get_data_all();
            }
        }
        else
        {
            test_p_millis = get_millis();
        }
    }
}
