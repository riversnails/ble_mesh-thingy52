
#ifndef TEST_DATA_TYPE_H__
#define TEST_DATA_TYPE_H__

#include <stdint.h>


#define IS_CLIENT false

/** Model Company ID */
#define CUSTOM_COMPANY_ID 0xFFFF

/** Maximum value of the onoff state, as defined in the Mesh Model Specification v1.0 */
#define CUSTOM_MAX        (0x01)

/** Shortest allowed length for the Set message. */
#define CUSTOM_SET_MINLEN 2
/** Longest allowed length for the Set message. */
#define CUSTOM_SET_MAXLEN (sizeof(custom_state_t) / sizeof(uint8_t))

/** Shortest allowed length for the Status message. */
#define CUSTOM_STATUS_MINLEN 1
/** Longest allowed length for the Status message. */
#define CUSTOM_STATUS_MAXLEN 3

#define DEVICE_MAXLEN 50

#define ADDR_MAXLEN 6

/** Generic On Off model message opcodes. */
typedef enum
{
    CUSTOM_OPCODE_SET = 0x8202,
    CUSTOM_OPCODE_SET_UNACKNOWLEDGED = 0x8203,
    CUSTOM_OPCODE_GET = 0x8201,
    CUSTOM_OPCODE_STATUS = 0x8204
} custom_opcode_t;

/** Structure containing value of the OnOff state */
typedef struct __attribute((packed))
{
  int8_t temp_intager;
  uint8_t temp_decimal;
  uint8_t humi;
  uint8_t ttl;
  uint8_t tid;
  uint8_t addr[ADDR_MAXLEN];
} custom_state_t;

/** Mandatory parameters for the Generic OnOff Set message. */
typedef custom_state_t custom_set_params_t;

/** Parameters for the Generic OnOff Status message. */
typedef custom_state_t custom_status_params_t;

typedef custom_state_t custom_set_msg_pkt_t;

typedef custom_state_t custom_status_msg_pkt_t;

#endif /* TEST_DATA_TYPE_H__ */