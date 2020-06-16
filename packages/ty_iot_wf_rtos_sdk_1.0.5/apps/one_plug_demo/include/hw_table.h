/***********************************************************
File: hw_table.h 
Author:qiuping.wang
Date: 2018-5-31
Description:
    硬件结构抽象表，表达通用硬件配置
    描述内容：
        1，wifi指示[数量(0~1)]
            驱动方式：高 低驱动
        2开关 插座控制通道(ctrl_channel)[1~n]
            继电器(relay)：高 低驱动
            按钮：高 低驱动或不驱动
            LED指示灯：高 低驱动或不驱动
        3.（2018.05.31）加入上电设备通道状态（0 默认上电断电）此外还有通电和断电记忆
***********************************************************/
#ifndef HW_TABLE_H
#define HW_TABLE_H

#include "tuya_gpio.h"
#include "tuya_key.h"
#include "tuya_led.h"
#include "tuya_iot_wifi_api.h"
#include "gw_intf.h"
#include "tuya_device.h"

#ifdef __HW_TABLE_GLOBALS
    #define __HW_TABLE_EXT
#else
    #define __HW_TABLE_EXT extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*************************micro define***********************
***********************************************************/
#define KEY_TIMER_MS       20    //key timer inteval
#define KEY_RST_TIME       5000            //按键重置时间 ms

#define DPID_NOT_EXIST (-1)

/***********************************************************
*************************variable define********************
***********************************************************/
typedef enum 
{
    IO_DRIVE_LEVEL_LOW,         //低电平有效
    IO_DRIVE_LEVEL_HIGH,        //高电平有效
    IO_DRIVE_LEVEL_NOT_EXIST    //该IO不存在
}IO_DRIVE_TYPE;

typedef struct
{
    IO_DRIVE_TYPE type;         // 有效电平类型
    TY_GPIO_PORT_E pin;         // 引脚号
}IO_CONFIG;

typedef struct {
    IO_CONFIG          io_cfg;
    LED_HANDLE         io_handle;
}
OUT_PUT_S;

typedef struct
{
    IO_CONFIG       relay;          // 继电器
    KEY_USER_DEF_S  button;         // 控制按键
    OUT_PUT_S       led;            // 状态指示灯
    INT_T           dpid;           // 该通道绑定的dpid
    INT_T           cd_dpid;        // 该通道绑定的倒计时dpid 小于0表示不存在
    INT_T           cd_sec;         // 通道倒计时 -1 停止
    INT_T           prtest_swti1_count;//厂测时用于按一下状态切换三次功能的计数器
    INT_T           respond_cd_count;
    BOOL_T          stat;           //通道状态 TRUE - 有效; FALSE - 无效
}CTRL_CHANNEL;

typedef struct
{
    INT_T           mode_dpid;          //该通道绑定的上电状态init_dpid 0 默认断电（当不做配置时默认断电）
    OUT_PUT_S       wifi_stat_led;      // wifi状态指示灯
    KEY_USER_DEF_S  rst_button;         // 重置按键
    TIMER_ID        switch_timer;       //厂测时用于按一下状态切换三次功能的定时器句柄
    INT_T           channel_num;
    CTRL_CHANNEL    *channels;          // ͨ导通列表 *!* 不要重新指向其它位置
}HW_TABLE;
extern HW_TABLE g_hw_table; 

typedef struct{
    BOOL_T is_count_down; 
    GW_WF_CFG_MTHD_SEL gwcm_mode_user;
    TIMER_ID cd_timer;
    INT_T cd_upload_period;
    TIMER_ID save_stat_timer;
    TIMER_ID wf_timer;
    BOOL_T is_save_stat; 
    TIMER_ID memory_left_timer;
    GW_WIFI_NW_STAT_E sys_wifi_stat;
    BOOL_T prod_flag;
}FUNC_SELECT;
extern FUNC_SELECT func_select;

/***********************************************************
*************************function define********************
***********************************************************/

/***********************************************************
*  Function:    hw_init
*  Input:       hw_key_process
*  Output:      hw_key_process
*  Return:      OPERATE_RET
***********************************************************/
__HW_TABLE_EXT \
OPERATE_RET init_hw(IN HW_TABLE *hw_table);

/***********************************************************
*  Function:    hw_set_channel
*  Input:       channel_no:  controled channel number  range[0, channel_num -1]
*               is_active 
*  Output:      none
*  Return:      none
***********************************************************/
__HW_TABLE_EXT \
VOID hw_set_channel(IN INT_T channel_no, IN BOOL_T is_active);

/***********************************************************
*  Function:    hw_set_channel_by_dpid
*  Input:       dpid
*               is_active
*  Output:
*  Return:
***********************************************************/
__HW_TABLE_EXT \
INT_T hw_set_channel_by_dpid(IN INT_T dpid, IN BOOL_T is_active);

/***********************************************************
*  Function:    hw_trig_channel
*  Input:       channel_no:  controled channel number  range[0, channel_num -1]
*  Output:
*  Return:
***********************************************************/
__HW_TABLE_EXT \
VOID hw_trig_channel(IN INT_T channel_no);

/***********************************************************
*  Function:    hw_set_wifi_led_stat
*  Input:       wifi_stat:  wifi connecting state
*  Output:      none
*  Return:      none
***********************************************************/
__HW_TABLE_EXT \
VOID hw_set_wifi_led_stat(IN GW_WIFI_NW_STAT_E wifi_stat);

/***********************************************************
*  Function:    hw_find_channel_by_cdpid
*  Input:       cddpid:        channel cd dpid
*  Output:      none
*  Return:      INT_T
***********************************************************/
__HW_TABLE_EXT \
INT_T hw_find_channel_by_cd_dpid(IN INT_T dpid);

/***********************************************************
*  Function:    get_channel_stat
*  Input:       idx: channel index
*  Output:      none
*  Return:      channel state : true\false
***********************************************************/
__HW_TABLE_EXT \
INT_T get_channel_stat(IN INT_T idx);

/***********************************************************
*  Function:    get_channel_cddpid
*  Input:       idx
*  Output:      none
*  Return:      cddpid
***********************************************************/
__HW_TABLE_EXT \
INT_T get_channel_cddpid(IN UCHAR_T idx);

/***********************************************************
*  Function:    get_channel_dpid
*  Input:       idx
*  Output:      none
*  Return:      dpid
***********************************************************/
__HW_TABLE_EXT \ 
INT_T get_channel_dpid(IN UCHAR_T idx);

/***********************************************************
*  Function:    prod_test_init_hw
*  Input:       hw_table
*  Output:      none
*  Return:      INT_T
***********************************************************/
__HW_TABLE_EXT \
OPERATE_RET prod_test_init_hw(IN OUT HW_TABLE *hw_table);

#ifdef __cplusplus
}
#endif

#endif
