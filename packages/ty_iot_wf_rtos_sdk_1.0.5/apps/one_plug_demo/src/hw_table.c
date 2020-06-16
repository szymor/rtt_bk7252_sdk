/***********************************************************
File: hw_table.c 
Author: qiuping.wang 
Date: 2019-01-17
Description:
    硬件结构抽象表，表达通用硬件配置。
    描述内容：
        1.wifi指示灯[数量(0~1)]：
            驱动方式：高、低驱动
        2.开关、插座控制通道(ctrl_channel)[1~n]
            继电器(relay): 高、低驱动
            按钮：高、低驱动或不存在(目前只能配置是否存在)
            LED指示灯：高、低驱动或不存在
        3.App支持自由配置上电状态：通电、断电和断电记忆
        4.(关于断电记忆)任意通道均可以自由选择是否断电记忆
            
***********************************************************/
#define __HW_TABLE_GLOBALS
#include <mem_pool.h> 
#include "hw_table.h"
#include "uni_log.h"
#include "cJSON.h"
/***********************************************************
*************************micro define***********************
***********************************************************/

#define IO_ACTIVE_TYPE(type)        ( ((type) == IO_DRIVE_LEVEL_HIGH) ? (TRUE) : (FALSE) )
#define IS_IO_TYPE_ALL_PERIPH(PERIPH) (((PERIPH) == IO_DRIVE_LEVEL_HIGH) ||\
                                       ((PERIPH) == IO_DRIVE_LEVEL_LOW)   ||\
                                       ((PERIPH) == IO_DRIVE_LEVEL_NOT_EXIST))

#define DRV_STATE_TYPE(stat, type)  ( (stat) ? (DRV_ACTIVE_TYPE(type)) : (DRV_PASSTIVE_TYPE(type)) )
#define DRV_ACTIVE_TYPE(type)       ( ((type) == IO_DRIVE_LEVEL_HIGH) ? (OL_HIGH) : (OL_LOW) )
#define DRV_PASSTIVE_TYPE(type)     ( ((type) == IO_DRIVE_LEVEL_HIGH) ? (OL_LOW)  : (OL_HIGH) )


/***********************************************************
*************************variable define********************
***********************************************************/
STATIC BOOL_T IS_CONNECTED = TRUE; 
STATIC CTRL_CHANNEL channels[] = 
{
    {
        .relay  = {.type = IO_DRIVE_LEVEL_HIGH, .pin = TY_GPIOA_14},
        .button = {TY_GPIOA_5,TRUE,LP_INVALID,0,50,NULL},
        .led.io_cfg = {.type = IO_DRIVE_LEVEL_NOT_EXIST },
        .dpid   = DP_SWITCH,
        .cd_dpid = DP_COUNT_DOWN,
        .prtest_swti1_count = 0
    }
};

/***********************************************************
*************************function define********************
***********************************************************/
STATIC VOID sys_realy_led_stat(VOID);
STATIC VOID hw_sys_net_stat(VOID);
extern VOID key_process(IN TY_GPIO_PORT_E port, IN PUSH_KEY_TYPE_E type, IN INT_T cnt);

HW_TABLE g_hw_table = 
{
    .wifi_stat_led.io_cfg = {.type = IO_DRIVE_LEVEL_LOW, .pin = TY_GPIOA_0},
    .rst_button = {TY_GPIOA_5,TRUE,LP_ONCE_TRIG,KEY_RST_TIME,50,key_process},
    .channels = channels
};
    
/***********************************************************
*  Function:    led_pin_reg
*  Input:       output    output pin and effective level  
*               led_handle
*  Output:      output    output pin and effective level
*  Return:      OPERATE_RET 
***********************************************************/
STATIC OPERATE_RET led_pin_reg(INOUT OUT_PUT_S *output)
{
    OPERATE_RET op_ret;

    if(NULL == output) {
        PR_ERR("NULL pointer");
        return OPRT_INVALID_PARM;
    }

    if(!IS_IO_TYPE_ALL_PERIPH(output->io_cfg.type)) {
        PR_ERR("IO type not define");
        return OPRT_INVALID_PARM;
    }
    
    if(output->io_cfg.type != IO_DRIVE_LEVEL_NOT_EXIST) {
       op_ret = tuya_create_led_handle(output->io_cfg.pin,\
                                      IO_ACTIVE_TYPE(output->io_cfg.type),\
                                      &(output->io_handle));
       if(OPRT_OK != op_ret) {
            PR_ERR("op_ret =%d",op_ret);
            return op_ret;
       }
    }

    return OPRT_OK;
}

/***********************************************************
*  Function:    led_pin_set_stat
*  Input:       output       led pin and effective level
                is_active->effective state
*  Output:      none
*  Return:      none
***********************************************************/
STATIC VOID led_pin_set_stat(IN OUT_PUT_S *output, IN BOOL_T is_active)
{
    // 检查LED_HANDLE防止崩溃
    if( NULL == output) {
        PR_ERR("NULL pointer");
        return;
    }

    if(!IS_IO_TYPE_ALL_PERIPH(output->io_cfg.type)) {
        PR_ERR("IO type not define");
        return OPRT_INVALID_PARM;
    }

    // IO不存在则跳过
    if(output->io_cfg.type != IO_DRIVE_LEVEL_NOT_EXIST) {
        tuya_set_led_light_type(output->io_handle,\
                                DRV_STATE_TYPE(is_active, output->io_cfg.type),\
                                0, 0);
    }
}

OPERATE_RET init_hw(IN OUT HW_TABLE *hw_table)
{
    OPERATE_RET op_ret = OPRT_OK;
    UCHAR_T i;
    
    op_ret = led_pin_reg(&(hw_table->wifi_stat_led));
    if(op_ret != OPRT_OK) {
        PR_ERR("wf init failed!", i);
        return op_ret;
    }

    op_ret = key_init(NULL,0,KEY_TIMER_MS);
    if(OPRT_OK != op_ret) {
        PR_ERR("key_init err:%d",op_ret);
        return op_ret;
    }
    if(hw_table->rst_button.call_back != NULL) {
        op_ret = reg_proc_key(&hw_table->rst_button);
        if(OPRT_OK != op_ret) {
            PR_ERR("reg_proc_key err:%d",op_ret);
            return;
        }
    }
    hw_table->channel_num = CNTSOF(channels);
    PR_DEBUG("channel num: %d", hw_table->channel_num);

    for(i = 0; i < hw_table->channel_num; i++) {
        op_ret = led_pin_reg(&(hw_table->channels[i].led));
        if(op_ret != OPRT_OK) {
            PR_ERR("wf init failed!", i);
            return op_ret;
        }
        if(hw_table->channels[i].button.call_back != NULL) {
            op_ret = reg_proc_key(&hw_table->channels[i].button);
            if(OPRT_OK != op_ret) {
                PR_ERR("reg_proc_key err:%d",op_ret);
                return;
            }
        }

        if(hw_table->channels[i].relay.type != IO_DRIVE_LEVEL_NOT_EXIST) {
        	op_ret = tuya_gpio_inout_set(hw_table->channels[i].relay.pin,FALSE);
            if(OPRT_OK != op_ret) {
                PR_ERR("tuya_gpio_inout_set_select err:%d",op_ret);
                return op_ret;
            }
        }

        hw_table->channels[i].cd_sec = -1;
        hw_set_channel(i,false);
    }
    return OPRT_OK;
}

STATIC VOID hw_sys_wf_led_state(VOID)
{
    if((func_select.sys_wifi_stat != STAT_UNPROVISION)&&(func_select.sys_wifi_stat != STAT_AP_STA_UNCFG)&&(!func_select.prod_flag)) {
        if(get_channel_stat(0)) {
            led_pin_set_stat(&(g_hw_table.wifi_stat_led), TRUE);
        }else {
            led_pin_set_stat(&(g_hw_table.wifi_stat_led), FALSE);
        }   
    }
}

VOID hw_set_channel(IN INT_T channel_no, IN BOOL_T is_active)
{
    OPERATE_RET op_ret = OPRT_OK;
    
    if(channel_no < 0 || channel_no >= g_hw_table.channel_num) {
        PR_ERR("channel_no error: %d", channel_no);
        return;
    }
    
    if(is_active) {
        PR_DEBUG("channel: %d true", channel_no);
        g_hw_table.channels[channel_no].stat = true;
        switch(g_hw_table.channels[channel_no].relay.type) {
            case IO_DRIVE_LEVEL_HIGH: {
                op_ret = tuya_gpio_write(g_hw_table.channels[channel_no].relay.pin,true);
                if( OPRT_OK != op_ret ) {
                    PR_ERR("tuya_gpio_write err:%d",op_ret);
                    return ;
                }
            }
            break;
            
            case IO_DRIVE_LEVEL_LOW: {
                op_ret = tuya_gpio_write(g_hw_table.channels[channel_no].relay.pin,FALSE);
                if( OPRT_OK != op_ret ) {
                    PR_ERR("tuya_gpio_write err:%d",op_ret);
                    return ;
                }
            }
            break;
            
            case IO_DRIVE_LEVEL_NOT_EXIST:
                break;
            default:
                break;
        }
    }else {
        PR_DEBUG("channel: %d false", channel_no);
        g_hw_table.channels[channel_no].stat = false;
        switch(g_hw_table.channels[channel_no].relay.type) {
            case IO_DRIVE_LEVEL_HIGH: {
                op_ret = tuya_gpio_write(g_hw_table.channels[channel_no].relay.pin,FALSE);
                if( OPRT_OK != op_ret ) {
                    PR_ERR("tuya_gpio_write err:%d",op_ret);
                    return ;
                }
            }
            break;
            
            case IO_DRIVE_LEVEL_LOW:{
                op_ret = tuya_gpio_write(g_hw_table.channels[channel_no].relay.pin,TRUE);
                if( OPRT_OK != op_ret ) {
                    PR_ERR("tuya_gpio_write err:%d",op_ret);
                    return ;
                }
            }
            break;
            
            case IO_DRIVE_LEVEL_NOT_EXIST:
                break;
            default:
                break;
        }
    }
    hw_sys_wf_led_state();
}

VOID hw_set_all_channel(IN BOOL_T is_active)
{
    INT_T i;
    for(i = 0; i < g_hw_table.channel_num; i++) {
        hw_set_channel(i,is_active);
        if( g_hw_table.channels[i].cd_sec != -1) {
             g_hw_table.channels[i].cd_sec = -1;
        }
    }
}

BOOL_T hw_get_all_channel_stat(VOID)
{
    INT_T i;
    BOOL_T is_every_active = TRUE;
    for(i = 0; i < g_hw_table.channel_num; i++) {
        is_every_active = is_every_active && g_hw_table.channels[i].stat;
    }

    return is_every_active;
}

VOID hw_trig_all_channel(IN BOOL_T is_every_active)
{
    hw_set_all_channel(!is_every_active);
}

VOID hw_trig_channel(IN INT_T channel_no)
{
    if(channel_no < 0 || channel_no >= g_hw_table.channel_num) {
        PR_ERR("channel_no error: %d", channel_no);
        return;
    }

    if(g_hw_table.channels[channel_no].stat == TRUE) {
        hw_set_channel(channel_no, FALSE);
    }else {
        hw_set_channel(channel_no, TRUE);
    }
}

INT_T hw_set_channel_by_dpid(IN INT_T dpid, IN BOOL_T is_active)
{
    INT_T i;
    for(i=0; i<g_hw_table.channel_num; ++i) {
        if(dpid == g_hw_table.channels[i].dpid) {
            hw_set_channel(i, is_active);
            return i;
        }
    }
    return -1;
}

INT_T hw_find_channel_by_cd_dpid(IN INT_T dpid)
{
    INT_T i;
    for(i=0; i<g_hw_table.channel_num; ++i) {
        if(dpid == g_hw_table.channels[i].cd_dpid) {
            return i;
        }
    }
    return -1;
}
INT_T hw_find_channel_by_button(IN TY_GPIO_PORT_E gpio_num)
{
    INT_T i;
    for(i=0; i<g_hw_table.channel_num; ++i) {
        if(gpio_num == g_hw_table.channels[i].button.port) {
            return i;
        }
    }
    return -1;
}

VOID hw_set_wifi_led_stat(IN GW_WIFI_NW_STAT_E wifi_stat)
{
    GW_WIFI_NW_STAT_E wf_stat;
    wf_stat = wifi_stat;
    PR_NOTICE("wifi status is :%d",wifi_stat);

    switch(wf_stat) {
        case STAT_UNPROVISION: {
             IS_CONNECTED = FALSE;
             tuya_set_led_light_type(g_hw_table.wifi_stat_led.io_handle,OL_FLASH_HIGH,250,LED_TIMER_UNINIT);
        }
        break;
        
        case STAT_AP_STA_UNCFG: {
             IS_CONNECTED = FALSE;
             tuya_set_led_light_type(g_hw_table.wifi_stat_led.io_handle,OL_FLASH_HIGH,1500,LED_TIMER_UNINIT);
        }
        break;

        case STAT_LOW_POWER: {
            IS_CONNECTED = FALSE;
            hw_sys_wf_led_state();
        }
        break;
        
        case STAT_AP_STA_DISC:
        case STAT_STA_DISC: {
            IS_CONNECTED = TRUE;
            hw_sys_wf_led_state();
        } 
        break;
        
        case STAT_AP_STA_CONN:
        case STAT_STA_CONN: {
            IS_CONNECTED = TRUE;
            hw_sys_wf_led_state();
            Start_boot_up();
        }
        break;
        
        case STAT_CLOUD_CONN: 
        case STAT_AP_CLOUD_CONN: {
            IS_CONNECTED = TRUE;
            hw_sys_wf_led_state();
            Start_boot_up();
        }
        break;
        
        default: {
            hw_sys_wf_led_state();
        } 
        break;
    }
    return;
}

UCHAR_T get_all_ch_num(VOID)
{
    return (g_hw_table.channel_num);
}

/***********************************************************
*  Function:    get_channel_stat
*  Input:       idx: channel index
*  Output:      none
*  Return:      channel state : true\false
***********************************************************/
INT_T get_channel_stat(IN INT_T idx)
{
    return (g_hw_table.channels[idx].stat);
}

/***********************************************************
*  Function:    get_channel_cddpid
*  Input:       idx
*  Output:      none
*  Return:      cddpid
***********************************************************/
INT_T get_channel_cddpid(IN UCHAR_T idx)
{   if(idx < g_hw_table.channel_num) {
        return (g_hw_table.channels[idx].cd_dpid);
    }else {
        return DPID_NOT_EXIST;
    }
}

/***********************************************************
*  Function:    get_channel_dpid
*  Input:       idx
*  Output:      none
*  Return:      dpid
***********************************************************/
INT_T get_channel_dpid(IN UCHAR_T idx)
{   if(idx < g_hw_table.channel_num) {
        return (g_hw_table.channels[idx].dpid);
    }else {
        return DPID_NOT_EXIST;
    }
}

STATIC VOID Protest_key_process(IN TY_GPIO_PORT_E port, IN PUSH_KEY_TYPE_E type, IN INT_T cnt)
{
    PR_DEBUG("port: %d",port);
    PR_DEBUG("type: %d",type);
    PR_DEBUG("cnt: %d",cnt);
    if(g_hw_table.rst_button.port== port){
        if(NORMAL_KEY == type) {
            if(IsThisSysTimerRun(g_hw_table.switch_timer)==false) {  
                sys_start_timer(g_hw_table.switch_timer,RELAY_ACT_INTER,TIMER_ONCE);
            }   
        }
     }
}

OPERATE_RET prod_test_init_hw(IN HW_TABLE *hw_table)
{
    OPERATE_RET op_ret = OPRT_OK;
    UCHAR_T i;
    op_ret = led_pin_reg(&(hw_table->wifi_stat_led));
    if(op_ret != OPRT_OK) {
        PR_ERR("wf init failed!");
        return op_ret;
    }
    
    op_ret = key_init(NULL,0,KEY_TIMER_MS);
    if(OPRT_OK != op_ret) {
        PR_ERR("key_init err:%d",op_ret);
        return op_ret;
    }

    hw_table->rst_button.call_back = Protest_key_process;
    if(hw_table->rst_button.call_back != NULL) {
        op_ret = reg_proc_key(&hw_table->rst_button);
        if(OPRT_OK != op_ret) {
            PR_ERR("reg_proc_key err:%d",op_ret);
            return;
        }
    }
    hw_table->channel_num = CNTSOF(channels);
    PR_DEBUG("channel num: %d", hw_table->channel_num);
    
    for(i = 0; i < hw_table->channel_num; i++) {
        op_ret = led_pin_reg(&(hw_table->channels[i].led));
        if(OPRT_OK != op_ret) {
            PR_ERR("ch[%d]led_pin_reg failed!", i);
            return op_ret;
        }

        if(hw_table->channels[i].button.call_back != NULL) {
            op_ret = reg_proc_key(&hw_table->channels[i].button);
            if(OPRT_OK != op_ret) {
                PR_ERR("reg_proc_key err:%d",op_ret);
                return;
            }
        }

        if(hw_table->channels[i].relay.type != IO_DRIVE_LEVEL_NOT_EXIST) {
            op_ret = tuya_gpio_inout_set(hw_table->channels[i].relay.pin,FALSE);
            if(OPRT_OK != op_ret) {
                PR_ERR("tuya_gpio_inout_set_select err:%d",op_ret);
                return op_ret;
            }
        }

        hw_table->channels[i].cd_sec = -1;
        hw_table->channels[i].stat = FALSE;

        hw_set_channel(i, TRUE);

    }

    tuya_set_led_light_type(hw_table->wifi_stat_led.io_handle,OL_FLASH_HIGH,250,LED_TIMER_UNINIT);
    
    return OPRT_OK;
}
