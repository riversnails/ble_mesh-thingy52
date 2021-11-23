
#include "log.h"
#include "rtt_input.h"

#include "device_addr_manager.h"
#include "sensor_manage.h"
#include "device_manager.h"
#include "test_data_type.h"
#include "app_timer.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void (*send_manager) (custom_set_params_t send_data);

static uint8_t save_tid[DEVICE_MAXLEN] = {0};
static uint8_t save_inst_tid[DEVICE_MAXLEN] = {0};
static bool is_available[DEVICE_MAXLEN] = {false};

static ble_tes_temperature_t * t_conv_data;
static ble_tes_humidity_t * h_conv_data;

static bool * start;

unsigned long millis = 0;

static void millis_handler(void * data)
{
  millis+=1;
}


APP_TIMER_DEF(millis_id);

void init_device_manager(ble_tes_temperature_t * t_pointer, 
                          ble_tes_humidity_t * h_pointer, bool * state)
{
    uint32_t err_code;
    err_code = app_timer_create(&millis_id, APP_TIMER_MODE_REPEATED, millis_handler);
    if(err_code != NRF_SUCCESS) while(1);
    err_code = app_timer_start(millis_id, APP_TIMER_TICKS(1), NULL);
    if(err_code != NRF_SUCCESS) while(1);
    t_conv_data = t_pointer;
    h_conv_data = h_pointer;
    start = state;
}

void init_send_manager(void * handler)
{
  send_manager = handler;
}

unsigned long get_millis()
{
    return millis;
}

uint8_t get_tid(uint8_t addr)
{
  return save_tid[addr];
}

void set_tid(uint8_t get_tid, uint8_t addr)
{
  save_tid[addr] = get_tid;
}

uint8_t get_inst_tid(uint8_t addr)
{
  return save_inst_tid[addr];
}

void set_inst_tid(uint8_t get_tid, uint8_t addr)
{
  save_inst_tid[addr] = get_tid;
}

bool get_available(uint8_t addr)
{
    return is_available[addr];
}

void set_available(bool available, uint8_t addr)
{
    is_available[addr] = available;
}


bool check_requset_server(uint8_t humi, int8_t temp_intager, uint8_t temp_decimal)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d, %d, %d, %d\n", humi, ttl, temp_decimal, addr);
    if(humi >= 101 && temp_intager == -50 && temp_decimal == 0) return true;
    else return false;
}

inst_packet_list_t unpack_inst_packet(uint8_t humi, int8_t temp_intager, uint8_t temp_decimal)
{
    if(check_requset_server(humi, temp_intager, temp_decimal))
    {
        return (inst_packet_list_t)(humi - 100);
    }
    return 0;
}

static void send_inst_requset(inst_packet_list_t inst, uint8_t *addr, uint8_t ttl)
{
    custom_set_params_t set_params;
    set_params.temp_intager = -50;
    set_params.temp_decimal = 0;
    set_params.humi = 100 + inst;
    set_params.ttl = ttl;
    set_params.tid = (uint8_t)get_millis();
    strcpy(set_params.addr, addr);
    //set_params.addr = addr;
    send_manager(set_params);
}

static void send_data_requset(uint8_t *addr, bool is_all)
{
    bool state1 = comp_addr(addr, get_this_addr()) == true && is_all == false;
    bool state2 = comp_addr(addr, get_this_addr()) == false && is_all == true;
    if(state1 || state2)
    {
        custom_set_params_t set_params;
        set_params.temp_intager = t_conv_data->integer;
        set_params.temp_decimal = t_conv_data->decimal;
        set_params.humi = *h_conv_data;
        set_params.ttl = 10;
        set_params.tid = (uint8_t)get_millis();
        strcpy(set_params.addr, get_this_addr());
        send_manager(set_params);
    }
}

bool packet_response(custom_state_t packet)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "pack_response\n");
    inst_packet_list_t state;
    
    state = unpack_inst_packet(packet.humi, packet.temp_intager, packet.temp_decimal);

    if(state)
    {
      switch (state)
      {
          case SEND_ADDR:
          {
              //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "case ADDR\n");

              if(comp_addr(packet.addr, get_this_addr()) ) 
              {
                  return false;
              }
              
              print_addr(packet.addr);
              
              //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "not me\n");
            
              #if IS_CLIENT
              send_inst_requset(SEND_ADDR, get_this_addr(), packet.ttl); // requset first
              #endif

              uint8_t num;
              if(get_num(&num, packet.addr) == false) // end processing this device
              {
                  #if IS_CLIENT
                  *start = true;
                  #endif

                  add_addr_node(packet.addr);

                  uint8_t num;
                  get_num(&num, packet.addr);
                  set_available(true, num);
                  print_addr(packet.addr);
              }
              #if IS_CLIENT
              else
              {
                  __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "failed! send next node!\n");
                  send_manager(packet);
              }
              #endif

              break;
          }
          case SEND_DATA:
          {
              //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "case DATA\n");
              #if IS_CLIENT
                  send_data_requset(packet.addr, false);
              #endif
              break;
          }
          case SEND_DATA_ALL:
          {
              #if IS_CLIENT
                  send_data_requset(packet.addr, true);
              #endif
              break;
          }
          default:
              __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "inst not found!\n");
              return false;
      }
      
      return true;
    }
    else
    {
     __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "inst not valid!\n");
    }

    return false;
}

void get_data_one_addr(uint8_t *addr)
{
    uint8_t num;
    if(get_num(&num, addr) == true)
    {
        send_inst_requset(SEND_DATA, addr, 10);
    }
    else
    {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "no addr find!\n");
    }
}

void get_data_all()
{
    send_inst_requset(SEND_DATA_ALL, get_this_addr(), 10);
}

void print_ttl(access_model_handle_t model_handle)
{
    uint8_t ttlll;
    uint8_t state;
    state = access_model_publish_ttl_get(model_handle, &ttlll);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "state : %d, ttl : %d\n", state, ttlll);
}