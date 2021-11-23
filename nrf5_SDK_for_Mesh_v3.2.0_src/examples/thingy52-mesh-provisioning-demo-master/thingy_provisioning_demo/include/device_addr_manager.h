#ifndef _DEVICE_ADDR_MANAGER_H__
#define _DEVICE_ADDR_MANAGER_H__

#include "test_data_type.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct addr_packet_t{
    uint8_t addr[ADDR_MAXLEN];
    uint8_t num;
    struct addr_packet_t * link_addr;
}addr_packet_t;


void add_addr_node(uint8_t *addr);

bool comp_addr(uint8_t *addr1, uint8_t *addr2);

bool search_node_addr(addr_packet_t * point_node, uint8_t *addr);

bool search_node_num(addr_packet_t * point_node, uint8_t num);

bool get_num(uint8_t * num, uint8_t *addr);

bool get_addr(uint8_t **addr, uint8_t num);

uint8_t get_this_num();

uint8_t* get_this_addr();

void print_addr(uint8_t *addr);

void print_all();

#endif