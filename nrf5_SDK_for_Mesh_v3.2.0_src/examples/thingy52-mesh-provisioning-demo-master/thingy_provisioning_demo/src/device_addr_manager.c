
#include "log.h"
#include "rtt_input.h"
#include "device_addr_manager.h"
#include <stdlib.h>

static addr_packet_t * start_node = NULL;
static addr_packet_t * end_node = NULL;
static uint8_t count_max = 0;

void add_addr_node(uint8_t *addr)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "add new node!\n");
    if(count_max >= DEVICE_MAXLEN)  return;

    addr_packet_t * new_addr_node = (addr_packet_t*)malloc(sizeof(addr_packet_t));
    
    strcpy(new_addr_node->addr, addr);
    //new_addr_node->addr = addr;
    new_addr_node->num = count_max;
    new_addr_node->link_addr = NULL;

    if(count_max == 0)
    {
        start_node = new_addr_node;
        end_node = new_addr_node;
    }
    else
    {
        end_node->link_addr = new_addr_node;
    }

    end_node = new_addr_node;
    count_max++;
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "add new node end!\n");
}

bool comp_addr(uint8_t *addr1, uint8_t *addr2)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "comp_addr\n");
    //print_addr(addr1);
    //print_addr(addr2);
    uint8_t count = 0;
    for(int i = 0; i < ADDR_MAXLEN; i++)
    {
        if(addr1[i] != addr2[i]) count++;
    }
    if(count == 0)  return true;
    else            return false;
}

bool search_node_addr(addr_packet_t * point_node, uint8_t *addr)
{
    addr_packet_t new_addr_node = *start_node;
    
    while(1)
    {
        if(comp_addr(addr, new_addr_node.addr))
        {
            *point_node = new_addr_node;
            return true;
        }
        if(new_addr_node.link_addr != NULL)  
        {
            new_addr_node = *new_addr_node.link_addr;
        }
        else break;
    }

    return false;
}

bool search_node_num(addr_packet_t * point_node, uint8_t num)
{
    addr_packet_t new_addr_node = *start_node;

    while(1)
    {
        if(new_addr_node.num == num)
        {
            *point_node = new_addr_node;
            return true;
        }
        if(new_addr_node.link_addr != NULL)  
        {
            new_addr_node = *new_addr_node.link_addr;
        }
        else break;
    }

    return false;
}

bool get_num(uint8_t * num, uint8_t *addr)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "get num\n");
    //print_addr(addr);

    addr_packet_t  new_addr_node;
    
    if(search_node_addr(&new_addr_node, addr))
    {
        *num = new_addr_node.num;
        return true;
    }
    else
    {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "no addr found!\n");
    }

    return false;
}

bool get_addr(uint8_t **addr, uint8_t num)
{
    //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "get addr\n");
    //print_addr(addr);

    addr_packet_t  new_addr_node;
    
    if(search_node_num(&new_addr_node, num))
    {
        *addr = new_addr_node.addr;
        return true;
    }
    else
    {
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "no num found!\n");
    }

    return false;
}

uint8_t get_this_num()
{
    return start_node->num;
}

uint8_t* get_this_addr()
{
    return start_node->addr;
}

void print_addr(uint8_t *addr)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%x:%x:%x:%x:%x:%x\n", 
          addr[5],addr[4],addr[3],addr[2],
          addr[1],addr[0]);
}

void print_all()
{
    addr_packet_t * new_addr_node = start_node;
    while(1)
    {
        print_addr(new_addr_node->addr);
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, "%d\n", new_addr_node->num);
        if(new_addr_node->link_addr != NULL) 
        {
            new_addr_node = new_addr_node->link_addr;
        }
        else break;
    }
}