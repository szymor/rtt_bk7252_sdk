/***********************************************************
*  File: tuya_device.c
*  Author: caojq
*  Date: 20200416
***********************************************************/
#define _TUYA_DEVICE_GLOBAL
#include "tuya_device.h"
#include "tuya_iot_wifi_api.h"
#include "tuya_led.h"
#include "tuya_gpio.h"
#include "tuya_key.h"
#include "hw_table.h"
#include "gw_intf.h"
#include "uni_log.h"
#include "gpio_test.h"
#include "kv_storge.h"
#include "cJSON.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
#define STORE_OFF_MOD "save_off_stat"
#define STORE_CHANGE "init_stat_save"
/***********************************************************
*************************variable define********************
***********************************************************/
FUNC_SELECT func_select = {
    .is_save_stat =  FALSE,
    .is_count_down = TRUE,
    .gwcm_mode_user = GWCM_LOW_POWER,
    .cd_upload_period = 30,
    .prod_flag = FALSE
};

/***********************************************************
*************************function define********************
***********************************************************/
VOID Start_boot_up(VOID);
STATIC VOID cd_timer_cb(IN UINT_T timerID,IN PVOID pTimerArg);
STATIC VOID read_saved_stat(VOID);
STATIC VOID save_stat_timer_cb(IN UINT_T timerID,IN PVOID pTimerArg);
STATIC VOID wf_timer_cb(IN UINT_T timerID,IN PVOID pTimerArg);
STATIC VOID memory_left_timer_cb(IN UINT_T timerID,IN PVOID pTimerArg);

/***********************************************************
*  Function: protest_switch_timer_cb
*  Input: none
*  Output: none
*  Return: none
*  Note: product test callback
************************************************************/
STATIC VOID protest_switch_timer_cb(IN UINT timerID, IN PVOID pTimerArg)
{
    STATIC UCHAR_T act_cnt = 0;
    STATIC UCHAR_T target_ch = 0;

    PR_DEBUG("all relay timer callback");

    if( target_ch < get_all_ch_num()) {
        hw_trig_channel(target_ch);
        act_cnt++;
    }

    if(target_ch >= get_all_ch_num()) {
        act_cnt = 0;
        target_ch = 0;
    }else if(act_cnt < 6) {
        sys_start_timer(g_hw_table.switch_timer, RELAY_ACT_INTER, TIMER_ONCE);
    }else{
        act_cnt = 0;
        target_ch++;
        sys_start_timer(g_hw_table.switch_timer, RELAY_ACT_INTER, TIMER_ONCE); 
    }
}

/***********************************************************
*  Function: prod_test
*  Input: none
*  Output: none
*  Return: none
*  Note: product test begin
************************************************************/
STATIC VOID prod_test(IN BOOL_T flag, IN CHAR_T rssi)
{
    OPERATE_RET op_ret = OPRT_OK;
    PR_DEBUG("dev_test_start_cb");
    PR_DEBUG("rssi:%d", rssi);

    if((rssi<-60) ||(flag == FALSE)) {
        PR_ERR("rssi:%d,flag:%d",rssi,flag);
        return;
    }

    op_ret = sys_add_timer(protest_switch_timer_cb,NULL,&g_hw_table.switch_timer);
    if(OPRT_OK != op_ret) {
        PR_ERR("sys_add_timer switch_timer err");
        return ;
    }

    op_ret = prod_test_init_hw(&g_hw_table);
    if(OPRT_OK != op_ret) {
        PR_ERR("prod_test_init_hw err");
        return ;
    }

    func_select.prod_flag = TRUE;
}

VOID key_process(IN TY_GPIO_PORT_E port, IN PUSH_KEY_TYPE_E type, IN INT_T cnt) 
{
    PR_NOTICE("port: %d",port);
    PR_NOTICE("type: %d",type);
    PR_NOTICE("cnt: %d",cnt);
    OPERATE_RET op_ret = OPRT_OK;

    if(LONG_KEY == type) {
        if(g_hw_table.rst_button.port== port) {
            PR_NOTICE("LONG PRESS KEY!");
            op_ret = tuya_iot_wf_gw_unactive();
            if(OPRT_OK != op_ret) {
                PR_ERR("tuya_iot_wf_gw_unactive op_ret:%d",op_ret);
                return;
            }
        }
    }else if(NORMAL_KEY == type) {
        if(g_hw_table.rst_button.port== port) {
            BOOL_T is_every_active = hw_get_all_channel_stat();
            hw_trig_all_channel(is_every_active);

            if(func_select.is_save_stat) {
                sys_start_timer(func_select.save_stat_timer, 5000, TIMER_ONCE);
           }

            op_ret = upload_all_dp_stat();
            if(OPRT_OK != op_ret) {
                PR_ERR("upload_channel_stat op_ret:%d",op_ret);
                return op_ret;
            }
        }
    }
}

/***********************************************************
*  Function: app_init
*  Input: none
*  Output: none
*  Return: none
*  Note: called by user_main
***********************************************************/
VOID app_init(VOID) 
{
    app_cfg_set(GWCM_LOW_POWER,prod_test);
    tuya_iot_oem_set(TRUE);
}
/***********************************************************
*  Function: status_changed_cb
*  Input: none
*  Output: none
*  Return: none
*  Note: net status changed by noted through callback
***********************************************************/
VOID status_changed_cb(IN CONST GW_STATUS_E status)
{
    PR_DEBUG("gw status changed to %d", status);

    return;
}

OPERATE_RET get_file_data_cb(IN CONST FW_UG_S *fw, IN CONST UINT_T total_len, IN CONST UINT_T offset,
                                     IN CONST BYTE_T *data, IN CONST UINT_T len, OUT UINT_T *remain_len, IN PVOID pri_data)
{
    PR_DEBUG("Rev File Data");
    PR_DEBUG("Total_len:%d ", total_len);
    PR_DEBUG("Offset:%d Len:%d", offset, len);

    return OPRT_OK;
}

VOID upgrade_notify_cb(IN CONST FW_UG_S *fw, IN CONST INT_T download_result, IN PVOID pri_data)
{
    PR_DEBUG("download  Finish");
    PR_DEBUG("download_result:%d", download_result);
}

VOID gw_ug_inform_cb(IN CONST FW_UG_S *fw)
{
    PR_DEBUG("Rev GW Upgrade Info");
    PR_DEBUG("fw->fw_url:%s", fw->fw_url);
   //PR_DEBUG("fw->fw_md5:%s", fw->fw_md5);
    PR_DEBUG("fw->sw_ver:%s", fw->sw_ver);
    PR_DEBUG("fw->file_size:%d", fw->file_size);

    tuya_iot_upgrade_gw(fw, get_file_data_cb, upgrade_notify_cb, NULL);
}

VOID dev_dp_query_cb(IN CONST TY_DP_QUERY_S *dp_qry)
{
    PR_DEBUG("Recv DP Query Cmd");
    OPERATE_RET op_ret = OPRT_OK;
    op_ret = upload_all_dp_stat();
    if(OPRT_OK != op_ret) {
        PR_ERR("upload_all_dp_stat op_ret:%d",op_ret);
        return;
    }
}

OPERATE_RET upload_channel_stat(IN UINT_T ch_idx)
{
    OPERATE_RET op_ret = OPRT_OK;

    INT_T count_sec = 0;
    INT_T dp_idx = 0;
    INT_T dp_cnt = 0;

    dp_cnt = (func_select.is_count_down)?2:1;
    TY_OBJ_DP_S *dp_arr = (TY_OBJ_DP_S *)Malloc(dp_cnt*SIZEOF(TY_OBJ_DP_S));
    if(NULL == dp_arr) {
        PR_ERR("malloc failed");
        return OPRT_MALLOC_FAILED;
    }

    memset(dp_arr, 0, dp_cnt*SIZEOF(TY_OBJ_DP_S));

    dp_arr[dp_idx].dpid = get_channel_dpid(ch_idx);
    dp_arr[dp_idx].type = PROP_BOOL;
    dp_arr[dp_idx].time_stamp = 0;
    dp_arr[dp_idx].value.dp_bool = get_channel_stat(ch_idx);
    dp_idx ++;

    if(func_select.is_count_down) {
        dp_arr[dp_idx].dpid = get_channel_cddpid(ch_idx);
        dp_arr[dp_idx].type = PROP_VALUE;
        dp_arr[dp_idx].time_stamp = 0;
        if(g_hw_table.channels[ch_idx].cd_sec >= 0) {
            dp_arr[dp_idx].value.dp_value = g_hw_table.channels[ch_idx].cd_sec;//get_cd_remain_sec(ch_idx);
        }else {
            dp_arr[dp_idx].value.dp_value = 0;
        }
        dp_idx ++;
    }

    if(dp_idx != dp_cnt) {
        PR_ERR("dp_idx:%d,dp_cnt:%d",dp_idx,dp_cnt);
        Free(dp_arr);
        dp_arr = NULL;
        return OPRT_COM_ERROR;
    }

    op_ret = dev_report_dp_json_async(get_gw_cntl()->gw_if.id,dp_arr,dp_cnt);
    Free(dp_arr);
    dp_arr = NULL;
    if(OPRT_OK != op_ret) {
        PR_ERR("dev_report_dp_json_async op_ret:%d",op_ret);
        return op_ret;
    }
    
    return OPRT_OK;
}

OPERATE_RET upload_all_dp_stat(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    INT_T count_sec = 0;
    INT_T ch_idx = 0, dp_idx = 0;
    INT_T dp_cnt = 0;

    if(func_select.is_count_down) {
        dp_cnt = g_hw_table.channel_num*2;
    }else {
        dp_cnt = g_hw_table.channel_num;
    }
    
    TY_OBJ_DP_S *dp_arr = (TY_OBJ_DP_S *)Malloc(dp_cnt*SIZEOF(TY_OBJ_DP_S));
    if(NULL == dp_arr) {
        PR_ERR("malloc failed");
        return OPRT_MALLOC_FAILED;
    }

    memset(dp_arr, 0, dp_cnt*SIZEOF(TY_OBJ_DP_S));
    for(ch_idx = 0,dp_idx = 0; (ch_idx < g_hw_table.channel_num)&&(dp_idx < dp_cnt); ch_idx++,dp_idx++) {
        dp_arr[dp_idx].dpid = get_channel_dpid(ch_idx);
        dp_arr[dp_idx].type = PROP_BOOL;
        dp_arr[dp_idx].time_stamp = 0;
        dp_arr[dp_idx].value.dp_bool = get_channel_stat(ch_idx);

        if(func_select.is_count_down) {
            dp_idx++;
            dp_arr[dp_idx].dpid = get_channel_cddpid(ch_idx);
            dp_arr[dp_idx].type = PROP_VALUE;
            dp_arr[dp_idx].time_stamp = 0;
            if(g_hw_table.channels[ch_idx].cd_sec >= 0) {
                dp_arr[dp_idx].value.dp_value = g_hw_table.channels[ch_idx].cd_sec;//get_cd_remain_sec(ch_idx);
            }else {
                dp_arr[dp_idx].value.dp_value = 0;
            }
        }
    }

    op_ret = dev_report_dp_json_async(get_gw_cntl()->gw_if.id,dp_arr,dp_cnt);
    Free(dp_arr);
    dp_arr = NULL;
    if(OPRT_OK != op_ret) {
        PR_ERR("dev_report_dp_json_async op_ret:%d",op_ret);
        return op_ret;
    }
    
    return OPRT_OK;
}

STATIC VOID deal_dps_proc(IN CONST TY_RECV_OBJ_DP_S *dp)
{
    OPERATE_RET op_ret = OPRT_OK;
    INT_T i = 0,ch_index;
    for(i = 0;i < dp->dps_cnt;i++) {
        PR_DEBUG("dpid:%d type:%d time_stamp:%d",dp->dps[i].dpid,dp->dps[i].type,dp->dps[i].time_stamp);
        switch(dp->dps[i].dpid) {
            case DP_SWITCH: {
                ch_index = hw_set_channel_by_dpid(dp->dps[i].dpid, dp->dps[i].value.dp_bool);
                if( ch_index >= 0) {
                    if((func_select.is_count_down)&&(g_hw_table.channels[ch_index].cd_sec >= 0)) {
                        g_hw_table.channels[ch_index].cd_sec = -1;
                    }
                    op_ret = upload_channel_stat(ch_index);
                    if(OPRT_OK != op_ret) {
                        PR_ERR("upload_channel_stat op_ret:%d",op_ret);
                        break;
                    }
                }
                if(func_select.is_save_stat) {
                    sys_start_timer(func_select.save_stat_timer, 5000, TIMER_ONCE);
                }
            }
            break;
            
            case DP_COUNT_DOWN: {
                ch_index = hw_find_channel_by_cd_dpid(dp->dps[i].dpid);
                if(ch_index >= 0) {
                    if(dp->dps[i].value.dp_value == 0) {
                        g_hw_table.channels[ch_index].cd_sec = -1;
                    }else {
                        g_hw_table.channels[ch_index].cd_sec = dp->dps[i].value.dp_value;
                    }
                    op_ret = upload_channel_stat(ch_index);
                    if(OPRT_OK != op_ret) {
                        PR_ERR("upload_channel_stat op_ret:%d",op_ret);
                        break;
                    }
                }
            }
            break;
            
            default:
            break;
        }
    }
}
VOID dev_obj_dp_cb(IN CONST TY_RECV_OBJ_DP_S *dp)
{
    PR_DEBUG("dp->cid:%s dp->dps_cnt:%d",dp->cid,dp->dps_cnt);
    deal_dps_proc(dp);
}

VOID dev_raw_dp_cb(IN CONST TY_RECV_RAW_DP_S *dp)
{
    PR_DEBUG("raw data dpid:%d",dp->dpid);
    PR_DEBUG("recv len:%d",dp->len);
    PR_DEBUG_RAW("\n");
    PR_DEBUG("end");
}

STATIC VOID __get_wf_status(IN CONST GW_WIFI_NW_STAT_E stat)
{
    OPERATE_RET op_ret = OPRT_OK;
    func_select.sys_wifi_stat = stat;
    hw_set_wifi_led_stat(stat);

    STATIC BOOL_T proc_flag = FALSE;
    if((STAT_STA_CONN <= stat)&&(proc_flag == FALSE)) {
        op_ret = upload_all_dp_stat();
        if(OPRT_OK != op_ret) {
            PR_ERR("upload_all_dp_stat op_ret:%d",op_ret);
            return;
        }
        proc_flag = TRUE;
    }
    return;
}

/***********************************************************
*  Function: gpio_test
*  Input: none
*  Output: none
*  Return: none
*  Note: For production testing
***********************************************************/
BOOL_T gpio_test(VOID)
{
    return gpio_test_all();
}

/***********************************************************
*  Function: pre_device_init
*  Input: none
*  Output: none
*  Return: none
*  Note: to initialize device before device_init
***********************************************************/
VOID pre_device_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    PR_DEBUG("%s",tuya_iot_get_sdk_info());
    PR_DEBUG("%s:%s",APP_BIN_NAME,DEV_SW_VERSION);
    PR_NOTICE("firmware compiled at %s %s", __DATE__, __TIME__);
    SetLogManageAttr(TY_LOG_LEVEL_INFO);
}

/***********************************************************
*  Function: 面板恢复出厂设置
*  Input: 
*  Output: 
*  Return: 
***********************************************************/
VOID gw_reset_cb(IN CONST GW_RESET_TYPE_E type)
{
    OPERATE_RET op_ret = OPRT_OK;
    if(GW_REMOTE_RESET_FACTORY != type) {
        return;
    }

    if(func_select.is_save_stat) {
        //恢复出厂设置的上电默认关闭
        op_ret = wd_common_fuzzy_delete(STORE_OFF_MOD); 
        if(OPRT_OK != op_ret) {
            PR_ERR("clear power_stat op_ret:%d",op_ret);
        }
    }

    return;
}

STATIC OPERATE_RET differ_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    
    if(func_select.is_save_stat) {
        PR_DEBUG("is_save_stat = %d",func_select.is_save_stat);
        read_saved_stat(); 

        op_ret = sys_add_timer(save_stat_timer_cb, NULL, &func_select.save_stat_timer);
        if(OPRT_OK != op_ret) {
            return op_ret;
        }
    }

    if(func_select.is_count_down) {
        op_ret = sys_add_timer(cd_timer_cb, NULL, &func_select.cd_timer);
        if(OPRT_OK != op_ret) {
            return op_ret;
        }else {
            PR_NOTICE("cd_timer ID:%d",func_select.cd_timer);
            sys_start_timer(func_select.cd_timer, 1000, TIMER_CYCLE);
        }
    }

    op_ret = sys_add_timer(wf_timer_cb,NULL, &func_select.wf_timer);
    if(OPRT_OK != op_ret) {
        PR_ERR("sys_add_timer failed! op_ret:%d", op_ret);
        return op_ret;
    }
    
    return OPRT_OK;
}

STATIC VOID wf_timer_cb(IN UINT_T timerID, IN PVOID pTimerArg)
{
    switch(func_select.sys_wifi_stat) {
        case STAT_UNPROVISION: {
            tuya_set_led_light_type(g_hw_table.wifi_stat_led.io_handle,OL_FLASH_HIGH,250,LED_TIMER_UNINIT);
        }
        break;

        case STAT_AP_STA_UNCFG: {
            tuya_set_led_light_type(g_hw_table.wifi_stat_led.io_handle,OL_FLASH_HIGH,1500,LED_TIMER_UNINIT);
        }
        break;

        default:
        break;
    }

}

VOID mf_user_callback(VOID)
{

}

/***********************************************************
*  Function: device_init 
*  Input: none
*  Output: none
*  Return: none
***********************************************************/
OPERATE_RET device_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    TY_IOT_CBS_S wf_cbs = {
        status_changed_cb,\
        gw_ug_inform_cb,\
        gw_reset_cb,\
        dev_obj_dp_cb,\
        dev_raw_dp_cb,\
        dev_dp_query_cb,\
        NULL,
    };

    op_ret = tuya_iot_wf_soc_dev_init_param(func_select.gwcm_mode_user,WF_START_SMART_FIRST, &wf_cbs, FIRMWAIRE_KEY, PRODUCT_KEY, DEV_SW_VERSION);
    if(OPRT_OK != op_ret) {
        PR_ERR("tuya_iot_wf_soc_dev_init err:%d",op_ret);
        return op_ret;
    }

    op_ret = init_hw(&g_hw_table);
    if(OPRT_OK != op_ret) {
        PR_ERR("init_hw err:%d",op_ret);
        return op_ret;
    }
    
    op_ret = tuya_iot_reg_get_wf_nw_stat_cb(__get_wf_status);
    if(OPRT_OK != op_ret) {
    PR_ERR("tuya_iot_reg_get_wf_nw_stat_cb err:%d",op_ret);
        return op_ret;
        }
    
    op_ret = differ_init();
    if(OPRT_OK != op_ret) {
        PR_ERR("differ_init err:%d",op_ret);
        return op_ret;
    }

    op_ret = sys_add_timer(memory_left_timer_cb, NULL, &func_select.memory_left_timer);
    if(OPRT_OK != op_ret) {
        return op_ret;
    }else {
        sys_start_timer(func_select.memory_left_timer,2*1000,TIMER_CYCLE);
    }

    INT_T size = tuya_hal_system_getheapsize();
    PR_DEBUG("device_init ok  free_mem_size:%d",size);
    return OPRT_OK;
}

STATIC VOID memory_left_timer_cb(IN UINT_T timerID, IN PVOID pTimerArg)
{
   PR_DEBUG("device_init ok  free_mem_size:%d",tuya_hal_system_getheapsize());
}

STATIC VOID cd_timer_cb(IN UINT_T timerID, IN PVOID pTimerArg)
{
    UCHAR_T i = 0;
    OPERATE_RET op_ret = OPRT_OK;

    for(i = 0; i<g_hw_table.channel_num; i++) {
        if(g_hw_table.channels[i].cd_sec < 0) {
            continue;
        }else {
            --g_hw_table.channels[i].cd_sec;
            if(g_hw_table.channels[i].cd_sec <= 0) {
                g_hw_table.channels[i].cd_sec = -1; 
                hw_trig_channel(i);
                op_ret = upload_channel_stat(i);
                if(OPRT_OK != op_ret) {
                    PR_ERR("upload_channel_stat op_ret:%d",op_ret);
                    return ;
                }
            }else {
                if(g_hw_table.channels[i].cd_sec % 30 == 0) {
                    op_ret = upload_channel_stat(i);
                    if(OPRT_OK != op_ret) {
                        PR_ERR("upload_channel_stat op_ret:%d",op_ret);
                        return;
                    }
                }
            }
        }
    }
}

STATIC VOID save_stat_timer_cb(IN UINT_T timerID,IN PVOID pTimerArg)
{
    OPERATE_RET op_ret = OPRT_OK;
    INT_T i;

    PR_DEBUG("save stat");
    IN CONST BYTE_T buff[48] = "{\"power\":[";
    for(i=0; i<g_hw_table.channel_num; ++i) {
        if(g_hw_table.channels[i].stat) {
            strcat(buff, "true");
        }
        else if(!g_hw_table.channels[i].stat) {
            strcat(buff, "false");
        }

        if(i < g_hw_table.channel_num -1) {
            strcat(buff, ",");
        }else {
            strcat(buff, "]}");
        }
    }
    PR_DEBUG("%s", buff);

    op_ret = wd_common_write(STORE_OFF_MOD,buff,strlen(buff));
    if(OPRT_OK != op_ret) {
        PR_DEBUG("kvs_write err:%02x",op_ret);
    }
}

VOID read_saved_stat(VOID)
{
    PR_DEBUG("_______________SAVE________________");
    OPERATE_RET op_ret = OPRT_OK;
    cJSON *root = NULL, *js_power = NULL, *js_ch_stat = NULL;
    UINT_T buff_len = 0;
    UCHAR_T *buff = NULL;

    INT_T i;
    op_ret = wd_common_read(STORE_OFF_MOD,&buff,&buff_len);
    if(OPRT_OK != op_ret) {
        PR_DEBUG("msf_get_single err:%02x",op_ret);
        return;
    }
    PR_DEBUG("read stat: %s", buff);
    root = cJSON_Parse(buff);
    Free(buff);
    if(NULL == root) {
        PR_ERR("cjson parse err");
        return;
    }

    js_power = cJSON_GetObjectItem(root, "power");
    if(NULL == js_power) {
        PR_ERR("cjson get power error");
        goto JSON_PARSE_ERR;
    }

    UCHAR_T count = cJSON_GetArraySize(js_power);
    if(count != g_hw_table.channel_num) {
        for(i = 0;i< g_hw_table.channel_num; ++i) {
            hw_set_channel(i, FALSE);
        }
        return;
    }
        
    for(i=0; i< g_hw_table.channel_num; ++i) {
        js_ch_stat = cJSON_GetArrayItem(js_power, i);
        if(js_ch_stat == NULL) {
            PR_ERR("cjson %d ch stat not found", i);
            goto JSON_PARSE_ERR;
        }else {
            if(js_ch_stat->type == cJSON_True) {
                hw_set_channel(i, TRUE);
            }else {
                hw_set_channel(i, FALSE);
            }  
        }
    }
JSON_PARSE_ERR:
    cJSON_Delete(root); root = NULL;

}

VOID Start_boot_up(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;
    op_ret = upload_all_dp_stat();
    if(OPRT_OK != op_ret) {
        PR_ERR("upload_channel_stat op_ret:%d",op_ret);
        return;
    }
}
