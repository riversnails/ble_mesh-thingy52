/* manage sensor

- temp
- humi
- pressure

*/

#include "sensor_manage.h"


/**@brief Function for converting the temperature sample.
 */
void temperature_conv_data(float in_temp, ble_tes_temperature_t * p_out_temp)
{
    float f_decimal;

    p_out_temp->integer = (int8_t)in_temp;
    f_decimal = in_temp - p_out_temp->integer;
    p_out_temp->decimal = (uint8_t)(f_decimal * 100.0f);
}


/**@brief Function for converting the humidity sample.
 */
void humidity_conv_data(uint8_t humid, ble_tes_humidity_t * p_out_humid)
{
   *p_out_humid = (uint8_t)humid;
}


/**@brief Function for converting the pressure sample.
 */
void pressure_conv_data(float in_press, ble_tes_pressure_t * p_out_press)
{
    float f_decimal;

    p_out_press->integer = (int32_t)in_press;
    f_decimal = in_press - p_out_press->integer;
    p_out_press->decimal = (uint8_t)(f_decimal * 100.0f);
}