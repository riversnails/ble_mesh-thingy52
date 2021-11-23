/* manage sensor

- temp
- humi
- pressure

*/

#ifndef _SENSOR_MANAGE_H__
#define _SENSOR_MANAGE_H__

#include "stdint.h"

#define PACKED(TYPE) TYPE __attribute__ ((packed))

typedef PACKED( struct
{
    int8_t  integer;
    uint8_t decimal;
}) ble_tes_temperature_t;

typedef PACKED( struct
{
    int32_t  integer;
    uint8_t  decimal;
}) ble_tes_pressure_t;

typedef uint8_t ble_tes_humidity_t;


void temperature_conv_data(float in_temp, ble_tes_temperature_t * p_out_temp);

void humidity_conv_data(uint8_t humid, ble_tes_humidity_t * p_out_humid);

void pressure_conv_data(float in_press, ble_tes_pressure_t * p_out_press);

#endif