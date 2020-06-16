#include "rtc_api.h"
#include "wait_api.h"
#include "tuya_hal_rtc.h"


void tuya_hal_rtc_init(void)
{
    rtc_init();
    rtc_write(0);
}


void tuya_hal_rtc_set_time(time_t write_time) 
{
    rtc_write(write_time);
}


time_t tuya_hal_rtc_get_time(void)
{
    time_t rtc_read_time= rtc_read();
    
    return rtc_read_time;
}


