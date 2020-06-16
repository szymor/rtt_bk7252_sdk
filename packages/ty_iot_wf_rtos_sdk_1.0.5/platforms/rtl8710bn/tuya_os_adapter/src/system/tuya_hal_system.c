/***********************************************************
*  File: uni_system.c
*  Author: nzy
*  Date: 120427
***********************************************************/
#define _UNI_SYSTEM_GLOBAL
#include <rtthread.h>
#include "task.h"
#include "semphr.h"
#include "basic_types.h"
#ifdef CONFIG_PLATFORM_8195A
    #include "rtl8195a.h"
#endif

#ifdef CONFIG_PLATFORM_8711B
#include "rtl8710b.h"
#include "rtl8710b_backup_reg.h"
#endif
#include "tuya_hal_system.h"
#include "tuya_hal_wifi.h"
#include "../errors_compat.h"

/***********************************************************
*************************micro define***********************
***********************************************************/


#if 0
#define LOGD PR_DEBUG
#define LOGT PR_TRACE
#define LOGN PR_NOTICE
#define LOGE PR_ERR
#else
#define LOGD(...) DiagPrintf("[SYS DEBUG]" __VA_ARGS__)
#define LOGT(...) DiagPrintf("[SYS TRACE]" __VA_ARGS__)
#define LOGN(...) DiagPrintf("[SYS NOTICE]" __VA_ARGS__)
#define LOGE(...) DiagPrintf("[SYS ERROR]" __VA_ARGS__)
#endif


#define SERIAL_NUM_LEN 32

//realtek supports 0,1,5
typedef enum {
    REASON_CPU_RESET_HAPPEN = 0,/*!< &&&&&&& 0 hardware reset or system reset (distinguish from 'REASON_SYS_RESET_HAPPEN' by Software) &&&&&&&77*/
    REASON_BOR2_RESET_HAPPEN,/*!<&&&&&&&& 1 watchdog reset **^&&&&&&&*/
    REASON_RTC_RESTORE, /*!< 2 this is SW set bit after rtc init */
    REASON_UARTBURN_BOOT,/*!< 3 this is SW set bit before reboot, for uart download */
    REASON_UARTBURN_DEBUG,/*!< 4 this is SW set bit before reboot, for uart download debug */
    REASON_SYS_RESET_HAPPEN,/*!<  &&&&&&&& 5 this is SW set bit before reboot, for distinguish 'REASON_CPU_RESET_HAPPEN'&&&&&&&&&&& */
    REASON_BOR2_RESET_TEMP, /*!<  BOR2 HW temp bit */
    REASON_SYS_BOR_DETECION /*!<  1: enable bor2 detection;  0: disable */
} RST_REASON_E;

/***********************************************************
*************************variable define********************
***********************************************************/
static char reason_buf[64];
static char serial_no[SERIAL_NUM_LEN+1] = {0};

/***********************************************************
*************************function define********************
***********************************************************/
/***********************************************************
*  Function: GetSystemTickCount 
*  Input: none
*  Output: none
*  Return: system tick count
***********************************************************/


SYS_TICK_T tuya_hal_get_systemtickcount(void)
{
    return (UINT)xTaskGetTickCount();
}

/***********************************************************
*  Function: GetTickRateMs 
*  Input: none
*  Output: none
*  Return: tick rate spend how many ms
***********************************************************/
uint32_t tuya_hal_get_tickratems(void)
{
    return (UINT)portTICK_RATE_MS;
}

/***********************************************************
*  Function: SystemSleep 
*  Input: msTime
*  Output: none 
*  Return: none
*  Date: 120427
***********************************************************/
void tuya_hal_system_sleep(IN const unsigned long msTime)
{
    vTaskDelay((msTime)/(portTICK_RATE_MS));
}

/***********************************************************
*  Function: SystemIsrStatus->direct system interrupt status
*  Input: none
*  Output: none 
*  Return: bool
***********************************************************/
bool tuya_hal_system_isrstatus(void)
{
    if(0 != __get_IPSR()) {
        return TRUE;
    }

    return FALSE;
}

/***********************************************************
*  Function: SystemReset 
*  Input: msTime
*  Output: none 
*  Return: none
*  Date: 120427
***********************************************************/
void tuya_hal_system_reset(void)
{
    sys_reset();
}

/***********************************************************
*  Function: uni_watchdog_init_and_start 
*  Input: timeval
*  Output: none 
*  Return: void *
***********************************************************/
void tuya_hal_watchdog_init_start(int timeval)
{
    watchdog_init(timeval * 1000); // setup timeval(s) watchdog

    watchdog_start();
}

/***********************************************************
*  Function: uni_watchdog_refresh 
*  Input: none
*  Output: none 
*  Return: void *
***********************************************************/
void tuya_hal_watchdog_refresh(void)
{
    watchdog_refresh();
}

/***********************************************************
*  Function: uni_watchdog_stop 
*  Input: none
*  Output: none 
*  Return: void *
***********************************************************/
void tuya_hal_watchdog_stop(void)
{
    watchdog_stop();
}


/***********************************************************
*  Function: SysGetHeapSize 
*  Input: none
*  Output: none 
*  Return: int-> <0 means don't support to get heapsize
***********************************************************/
int tuya_hal_system_getheapsize(void)
{
    return (int)xPortGetFreeHeapSize();
}

/***********************************************************
*  Function: GetSerialNo 
*  Input: none
*  Output: none 
*  Return: char *->serial number
***********************************************************/
char *tuya_hal_get_serialno(void)
{
    // if the device have unique serial number
    // then add get serial number code to serial_no array

    // if don't have unique serial number,then use mac addr
   
    int op_ret = OPRT_OK;
    NW_MAC_S mac1;
    op_ret = tuya_hal_wifi_get_mac(WF_STATION,&mac1);
    if(op_ret != OPRT_OK) {
        return NULL;
    }

    memset(serial_no,'\0',sizeof(serial_no));
    sprintf(serial_no,"%02x%02x%02x%02x%02x%02x",mac1.mac[0],mac1.mac[1],\
            mac1.mac[2],mac1.mac[3],mac1.mac[4],mac1.mac[5]);

    if(0 == serial_no[0]) {
        return NULL;
    }
    return serial_no;
}

/***********************************************************
*  Function: system_get_rst_info 
*  Input: none
*  Output: none 
*  Return: char *->reset reason
***********************************************************/
char *tuya_hal_system_get_rst_info(void)
{
    #ifdef CONFIG_PLATFORM_8711B
    int value_bk0 = BKUP_Read(BKUP_REG0);
    unsigned char value = value_bk0&0xFF;
    unsigned char rtn_value;

    switch(value) {
        case REASON_CPU_RESET_HAPPEN:{ rtn_value = 0;}break;
        case BIT_CPU_RESET_HAPPEN:{ rtn_value = 1; BKUP_Clear(BKUP_REG0, BIT_CPU_RESET_HAPPEN);}break;
        case BIT_BOR2_RESET_HAPPEN: {rtn_value = 2; BKUP_Clear(BKUP_REG0, BIT_BOR2_RESET_HAPPEN);}break;
        case BIT_RTC_RESTORE: {rtn_value = 3; BKUP_Clear(BKUP_REG0, BIT_RTC_RESTORE);}break;
        case BIT_UARTBURN_BOOT: {rtn_value = 4; BKUP_Clear(BKUP_REG0, BIT_UARTBURN_BOOT);}break;
        case BIT_UARTBURN_DEBUG: {rtn_value = 5; BKUP_Clear(BKUP_REG0, BIT_UARTBURN_DEBUG);}break;
        case BIT_SYS_RESET_HAPPEN: {rtn_value = 6; BKUP_Clear(BKUP_REG0, BIT_SYS_RESET_HAPPEN);}break;
        case BIT_BOR2_RESET_TEMP: {rtn_value = 7; BKUP_Clear(BKUP_REG0, BIT_BOR2_RESET_TEMP);}break;
        case BIT_SYS_BOR_DETECION: {rtn_value = 8; BKUP_Clear(BKUP_REG0, BIT_SYS_BOR_DETECION);}break;
        default: {rtn_value = 9; BKUP_Clear(BKUP_REG0, 0x01);BKUP_Clear(BKUP_REG0, 0x02);BKUP_Clear(BKUP_REG0, 0x04);\
                    BKUP_Clear(BKUP_REG0, 0x08);BKUP_Clear(BKUP_REG0, 0x10);BKUP_Clear(BKUP_REG0, 0x20);\
                    BKUP_Clear(BKUP_REG0, 0x40);BKUP_Clear(BKUP_REG0, 0x80);}break;
    }

    sprintf(reason_buf,"reset reset reason num %d",rtn_value);//rst reason num
    LOGD("value:0x%x %s\n",value,reason_buf);

    return reason_buf;
    #else
    static char reason_buf[64] = "do't support get reset reason";
    return reason_buf;
    #endif
}

bool tuya_hal_system_rst_reason_poweron(void)
{
    int value_bk0 = BKUP_Read(BKUP_REG0);
    unsigned char value = value_bk0&0xFF;
    unsigned char rtn_value;

    switch(value) {
        case REASON_CPU_RESET_HAPPEN:{ rtn_value = 0;}break;
        case BIT_CPU_RESET_HAPPEN:{ rtn_value = 1; BKUP_Clear(BKUP_REG0, BIT_CPU_RESET_HAPPEN);}break;
        case BIT_BOR2_RESET_HAPPEN: {rtn_value = 2; BKUP_Clear(BKUP_REG0, BIT_BOR2_RESET_HAPPEN);}break;
        case BIT_RTC_RESTORE: {rtn_value = 3; BKUP_Clear(BKUP_REG0, BIT_RTC_RESTORE);}break;
        case BIT_UARTBURN_BOOT: {rtn_value = 4; BKUP_Clear(BKUP_REG0, BIT_UARTBURN_BOOT);}break;
        case BIT_UARTBURN_DEBUG: {rtn_value = 5; BKUP_Clear(BKUP_REG0, BIT_UARTBURN_DEBUG);}break;
        case BIT_SYS_RESET_HAPPEN: {rtn_value = 6; BKUP_Clear(BKUP_REG0, BIT_SYS_RESET_HAPPEN);}break;
        case BIT_BOR2_RESET_TEMP: {rtn_value = 7; BKUP_Clear(BKUP_REG0, BIT_BOR2_RESET_TEMP);}break;
        case BIT_SYS_BOR_DETECION: {rtn_value = 8; BKUP_Clear(BKUP_REG0, BIT_SYS_BOR_DETECION);}break;
        default: {rtn_value = 9; BKUP_Clear(BKUP_REG0, 0x01);BKUP_Clear(BKUP_REG0, 0x02);BKUP_Clear(BKUP_REG0, 0x04);\
                    BKUP_Clear(BKUP_REG0, 0x08);BKUP_Clear(BKUP_REG0, 0x10);BKUP_Clear(BKUP_REG0, 0x20);\
                    BKUP_Clear(BKUP_REG0, 0x40);BKUP_Clear(BKUP_REG0, 0x80);}break;
    }
    LOGD("rst reason:%d\n", rtn_value);
    if (REASON_CPU_RESET_HAPPEN == rtn_value) {
        return TRUE;
    }
    return FALSE;
}
/***********************************************************
*  Function: tuya_random
*  Input: none
*  Output: none
*  Return: random data in INT
***********************************************************/
uint32_t tuya_hal_random(void)
{
    uint8_t data[4] = {0};
    tuya_hal_get_random_data(data, 4, 0);
    return data[0] + (data[1]<<8) + (data[2]<<16) + (data[3]<<24);
}

/***********************************************************
*  Function: tuya_get_random_data
*  Input: dst size
*  Output: none
*  Return: void
***********************************************************/
int tuya_hal_get_random_data(uint8_t* dst, int size, uint8_t range)
{
    if(range == 0)
        range = 0xFF;

    int i;
    static uint8_t exec_flag = FALSE;
    extern random_seed;
    if(!exec_flag) {
        srand(random_seed);
        exec_flag = TRUE;
    }
    for(i = 0; i< size; i++) {
        int val =  rand() + random_seed;
        dst[i] = val % range;
    }
    return OPRT_OK;
}


/***********************************************************
*  Function: tuya_set_lp_mode
*  Input: lp_enable
*  Output: none
*  Return: void
***********************************************************/
static bool is_lp_enable = FALSE;
void tuya_hal_set_lp_mode(bool lp_enable)
{
    is_lp_enable = lp_enable;
}

/***********************************************************
*  Function: tuya_get_lp_mode
*  Input: none
*  Output: none
*  Return: bool
***********************************************************/
bool tuya_hal_get_lp_mode(void)
{
    return is_lp_enable;
}
