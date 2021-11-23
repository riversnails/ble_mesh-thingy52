#ifndef _DEVICE_MANAGER_H__
#define _DEVICE_MANAGER_H__

#include "test_data_type.h"
#include "sensor_manage.h"
#include "device_addr_manager.h"
#include "access.h"
#include "access_config.h"
#include <stdint.h>
#include <stdbool.h>


typedef enum
{
  PACKET_fAILED = 1,
  SEND_ADDR,
  SEND_DATA,
  SEND_DATA_ALL,

}inst_packet_list_t;


void init_device_manager(ble_tes_temperature_t * t_pointer, ble_tes_humidity_t * h_pointer, bool * state);

void init_send_manager(void * handler);

unsigned long get_millis();

uint8_t get_tid(uint8_t addr);

void set_tid(uint8_t get_tid, uint8_t addr);

uint8_t get_inst_tid(uint8_t addr);

void set_inst_tid(uint8_t get_tid, uint8_t addr);

bool get_available(uint8_t addr);

void set_available(bool available, uint8_t addr);

bool check_requset_server(uint8_t humi, int8_t temp_intager, uint8_t temp_decimal);

inst_packet_list_t unpack_inst_packet(uint8_t humi, int8_t temp_intager, uint8_t temp_decimal);

bool packet_response(custom_state_t packet);

void get_data_one_addr(uint8_t *addr);

void get_data_all();

void print_ttl(access_model_handle_t model_handle);

#endif