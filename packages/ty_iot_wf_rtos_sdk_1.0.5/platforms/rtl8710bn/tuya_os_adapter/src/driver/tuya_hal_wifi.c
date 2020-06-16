/***********************************************************
*  File: wifi_hwl.c
*  Author: nzy
*  Date: 20170914
***********************************************************/
#define _WIFI_HWL_GLOBAL
#include "tuya_hal_wifi.h"
#include "platform_opts.h"
#include <wifi/wifi_conf.h>
#include <wifi/wifi_util.h>
#include "lwip/inet.h"
#include "lwip_netconf.h"
#include "wifi_conf.h"
#include "freertos_pmu.h"
#include "wifi_util.h"
#include "netif.h"
#include "tuya_os_adapter.h"
#include "../system/tuya_hal_system_internal.h"
#include "../errors_compat.h"


/***********************************************************
*************************micro define***********************
***********************************************************/

#if 0
#define LOGD PR_DEBUG
#define LOGN PR_NOTICE
#define LOGE PR_ERR
#else
#define LOGD(...) DiagPrintf("[WIFI DEBUG]" __VA_ARGS__)
#define LOGN(...) DiagPrintf("[WIFI NOTICE]" __VA_ARGS__)
#define LOGE(...) DiagPrintf("[WIFI ERROR]" __VA_ARGS__)
#endif

#define SCAN_MAX_AP 64
typedef struct {
    AP_IF_S *ap_if;
    uint8_t ap_if_nums;
    uint8_t ap_if_count;
    SEM_HANDLE sem_handle;
}SACN_AP_RESULT_S;

#define NETIF_STA_IDX 0 // lwip station/lwip ap interface  station/soft ap mode
#define NETIF_AP_IDX 1 // lwip ap interface
extern struct netif xnetif[NET_IF_NUM];

#define UNVALID_SIGNAL -127
#define SCAN_WITH_SSID 1
/***********************************************************
*************************variable define********************
***********************************************************/
static WF_WK_MD_E wf_mode = WWM_STATION; //WWM_LOWPOWER
static SNIFFER_CALLBACK snif_cb = NULL;
static THREAD_HANDLE dhcp_thrd = NULL;

extern unsigned char psk_essid[NET_IF_NUM][32+4];
extern unsigned char psk_passphrase[NET_IF_NUM][64 + 1];
extern unsigned char wpa_global_PSK[NET_IF_NUM][20 * 2];
extern unsigned char psk_passphrase64[64 + 1];

static uint8_t tuya_ssid[33] = {0};
static uint8_t tuya_pwd[65] = {0};

static uint8_t ap_not_find = FALSE;
static unsigned char g_ap_num = 3;
/***********************************************************
*************************function define********************
***********************************************************/
static void __dhcp_thread(void* pArg);
static void __hwl_promisc_callback(unsigned char *buf, unsigned int len, void* userdata);


static rtw_result_t __hwl_wifi_scan_cb( rtw_scan_handler_result_t* malloced_scan_result)
{
    SACN_AP_RESULT_S *scan_res = (SACN_AP_RESULT_S *)malloced_scan_result->user_data;

    if(malloced_scan_result->scan_complete != RTW_TRUE) {
        scan_res->ap_if[scan_res->ap_if_count].rssi = (int8_t)malloced_scan_result->ap_details.signal_strength;
        scan_res->ap_if[scan_res->ap_if_count].channel = malloced_scan_result->ap_details.channel;
        memcpy(scan_res->ap_if[scan_res->ap_if_count].bssid,\
               malloced_scan_result->ap_details.BSSID.octet,\
               sizeof(scan_res->ap_if[scan_res->ap_if_count].bssid));
        memcpy(scan_res->ap_if[scan_res->ap_if_count].ssid,\
               malloced_scan_result->ap_details.SSID.val,\
               malloced_scan_result->ap_details.SSID.len);
        scan_res->ap_if[scan_res->ap_if_count].s_len = malloced_scan_result->ap_details.SSID.len;
        scan_res->ap_if_count++;
    }else {
        tuya_hal_semaphore_post(scan_res->sem_handle);
    }

    return RTW_SUCCESS;
}

/***********************************************************
*  Function: hwl_wf_all_ap_scan
*  Input: none
*  Output: ap_ary num
*  Return: int
***********************************************************/
int tuya_hal_wifi_all_ap_scan(AP_IF_S **ap_ary, uint32_t *num)
{
    if(NULL == ap_ary || NULL == num) {
        return OPRT_INVALID_PARM;
    }

    int op_ret = OPRT_OK;
    SACN_AP_RESULT_S *scan_res = tuya_hal_internal_malloc(sizeof(SACN_AP_RESULT_S));
    if(NULL == scan_res) {
        return OPRT_MALLOC_FAILED;
    }
    memset(scan_res,0,sizeof(SACN_AP_RESULT_S));

    scan_res->ap_if = tuya_hal_internal_malloc(sizeof(AP_IF_S) * SCAN_MAX_AP);
    if(NULL == scan_res->ap_if) {
        op_ret = OPRT_MALLOC_FAILED;
        goto ERR_RET;
    }
    memset(scan_res->ap_if,0,sizeof(AP_IF_S) * SCAN_MAX_AP);
    scan_res->ap_if_nums = SCAN_MAX_AP;

    op_ret = tuya_hal_semaphore_create_init(&scan_res->sem_handle, 0, 2);
    if(OPRT_OK != op_ret) {
        goto ERR_RET;
    }

    int ret  = 0;
    ret = wifi_scan_networks(__hwl_wifi_scan_cb, scan_res);
    if(ret != RTW_SUCCESS) {
        op_ret = OPRT_WF_AP_SACN_FAIL;
        goto ERR_RET;
    }
    tuya_hal_semaphore_wait(scan_res->sem_handle);

    *ap_ary = scan_res->ap_if;
    *num = scan_res->ap_if_count;

    tuya_hal_semaphore_release(scan_res->sem_handle);
    tuya_hal_internal_free(scan_res);

    return OPRT_OK;

ERR_RET:
    tuya_hal_semaphore_release(scan_res->sem_handle);
    tuya_hal_internal_free(scan_res->ap_if);
    tuya_hal_internal_free(scan_res);

    return op_ret;
}

static rtw_result_t __hwl_wifi_assign_scan_cb( rtw_scan_handler_result_t* malloced_scan_result )
{
    SACN_AP_RESULT_S *scan_res = (SACN_AP_RESULT_S *)malloced_scan_result->user_data;

    if(malloced_scan_result->scan_complete != RTW_TRUE) {
        if((malloced_scan_result->ap_details.SSID.len == scan_res->ap_if->s_len) && \
            (0 == memcmp(scan_res->ap_if->ssid, \
                         malloced_scan_result->ap_details.SSID.val,\
                         malloced_scan_result->ap_details.SSID.len))) {
            if(scan_res->ap_if->rssi == UNVALID_SIGNAL) {                 
                 scan_res->ap_if->rssi = (int8_t)malloced_scan_result->ap_details.signal_strength;
            }else {
                if((int8_t)malloced_scan_result->ap_details.signal_strength > scan_res->ap_if->rssi) {
                    scan_res->ap_if->rssi = (int8_t)malloced_scan_result->ap_details.signal_strength;
                }
            }

            scan_res->ap_if->channel = malloced_scan_result->ap_details.channel;
            memcpy(scan_res->ap_if->bssid,\
                   malloced_scan_result->ap_details.BSSID.octet,\
                   sizeof(scan_res->ap_if->bssid));

            scan_res->ap_if->s_len = malloced_scan_result->ap_details.SSID.len;
            scan_res->ap_if_count++;
        }
    }else {
        tuya_hal_semaphore_post(scan_res->sem_handle);
    }

    return RTW_SUCCESS;
}

static rtw_result_t _hwl_wf_scan_network_with_ssid_cb(char*buf, int buflen, char *target_ssid, void *user_data)
{
    SACN_AP_RESULT_S *scan_res = (SACN_AP_RESULT_S *)user_data;
    int plen = 0;
 
    while(plen < buflen){
        unsigned char len, ssid_len, channel;
        char *mac;
        int rssi, i;

        // len offset = 0
        len = (int)*(buf + plen);
        // check end
        if(len == 0) break;
        // mac
        mac = buf + plen + 1;
        // rssi
        rssi = *(int*)(buf + plen + 1 + 6);
        // channel offset = 13
        channel = *(buf + plen + 13);
        if(scan_res->ap_if->rssi == UNVALID_SIGNAL) {                 
            scan_res->ap_if->rssi = (int8_t)rssi;
            scan_res->ap_if->channel = channel;
            memcpy(scan_res->ap_if->bssid, mac, sizeof(scan_res->ap_if->bssid));
        } else {
            if((int8_t)rssi > scan_res->ap_if->rssi) {
                scan_res->ap_if->rssi = (int8_t)rssi;
                scan_res->ap_if->channel = channel;
                memcpy(scan_res->ap_if->bssid, mac, sizeof(scan_res->ap_if->bssid));
            }
        }
        // ssid offset = 14
        ssid_len = len - 14;
        scan_res->ap_if->s_len = ssid_len;
        scan_res->ap_if_count++;
        plen += len;
    }

    tuya_hal_semaphore_post(scan_res->sem_handle);
    return RTW_SUCCESS;
}

/***********************************************************
*  Function: hwl_wf_assign_ap_scan
*  Input: ssid
*  Output: ap
*  Return: int
***********************************************************/
int tuya_hal_wifi_assign_ap_scan(const char *ssid, AP_IF_S **ap)
{
    if(NULL == ssid || NULL == ap) {
        return OPRT_INVALID_PARM;
    }

    int op_ret = OPRT_OK;
    SACN_AP_RESULT_S *scan_res = tuya_hal_internal_malloc(sizeof(SACN_AP_RESULT_S));
    if(NULL == scan_res) {
        return OPRT_MALLOC_FAILED;
    }
    memset(scan_res,0,sizeof(SACN_AP_RESULT_S));

    scan_res->ap_if = tuya_hal_internal_malloc(sizeof(AP_IF_S));
    if(NULL == scan_res->ap_if) {
        op_ret = OPRT_MALLOC_FAILED;
        goto ERR_RET;
    }
    memset(scan_res->ap_if,0,sizeof(AP_IF_S));
    scan_res->ap_if->rssi = UNVALID_SIGNAL;
    scan_res->ap_if_nums = 1;
    memcpy(scan_res->ap_if->ssid, ssid,strlen(ssid)+1);
    scan_res->ap_if->s_len = strlen(ssid);
    
    op_ret = tuya_hal_semaphore_create_init(&scan_res->sem_handle, 0, 2);
    if(OPRT_OK != op_ret) {
        goto ERR_RET;
    }

    int ret  = 0;
    scan_res->ap_if_count = 0;
#if SCAN_WITH_SSID
   //brief: Initiate a scan to search for 802.11 networks with specified SSID.
    u32 scan_buflen = 1024;
    ret = wifi_scan_networks_with_ssid(_hwl_wf_scan_network_with_ssid_cb, scan_res, scan_buflen, ssid, strlen(ssid));
#else
    ret = wifi_scan_networks(__hwl_wifi_assign_scan_cb, scan_res);
#endif
    if(ret != RTW_SUCCESS) {
        op_ret = OPRT_WF_AP_SACN_FAIL;
        goto ERR_RET;
    }
    tuya_hal_semaphore_wait(scan_res->sem_handle);

    if(0 == scan_res->ap_if_count) {
        op_ret = OPRT_WF_NOT_FIND_ASS_AP;
        LOGN("not find scan ssid (%s)\n",ssid);
        goto ERR_RET;
    }

    LOGD("scan count:%d\n",scan_res->ap_if_count);
    *ap = scan_res->ap_if;
    #if 0
    int i = 0;
    for(i =0 ; i < scan_res->ap_if_count; i ++) 
    {
        LOGN("rssi:%d ",scan_res->ap_if[i].rssi);
    }
    #endif
    tuya_hal_semaphore_release(scan_res->sem_handle);
    tuya_hal_internal_free(scan_res);

    return OPRT_OK;

ERR_RET:
    tuya_hal_semaphore_release(scan_res->sem_handle);
    tuya_hal_internal_free(scan_res->ap_if);
    tuya_hal_internal_free(scan_res);

    return op_ret;
}

/***********************************************************
*  Function: hwl_wf_release_ap
*  Input: ap
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_release_ap(AP_IF_S *ap)
{
    if(NULL == ap) {
        return OPRT_INVALID_PARM;
    }

    tuya_hal_internal_free(ap);
    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_set_cur_channel
*  Input: chan
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_set_cur_channel(const uint8_t chan)
{
    int ret = 0;
    ret = wifi_set_channel(chan);
    if(ret < 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_get_cur_channel
*  Input: none
*  Output: chan
*  Return: int
***********************************************************/
int tuya_hal_wifi_get_cur_channel(uint8_t *chan)
{
    int ret = 0;
    ret = wifi_get_channel(chan);
    if(ret < 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static void __hwl_promisc_callback(unsigned char *buf, unsigned int len, void* userdata)
{
    if(NULL == snif_cb) {
        return;
    }
    
    ieee80211_frame_info_t *promisc_info = (ieee80211_frame_info_t *)userdata;

#if CONFIG_UNSUPPORT_PLCPHDR_RPT
    rtw_rx_type_t type = promisc_info->type;
    if(type == RTW_RX_UNSUPPORT) {
        rtw_rx_info_t *rx_info = (rtw_rx_info_t *)buf;
        // special handle for LDPC/MIMO pkt
        // printf("rssi:%d filter:%d length:%04x\n", rx_info->rssi, rx_info->filter, rx_info->length - 0x42);

        WLAN_FRAME_S wl_frm;
        memset(&wl_frm,0,sizeof(WLAN_FRAME_S));

        wl_frm.frame_type = WFT_MIMO_DATA;
        wl_frm.frame_data.mimo_info.rssi = rx_info->rssi;
        wl_frm.frame_data.mimo_info.len = rx_info->length;
        wl_frm.frame_data.mimo_info.channel = rx_info->channel;
        if(rx_info->filter == 2)
            wl_frm.frame_data.mimo_info.type = MIMO_TYPE_2X2;
        else if(rx_info->filter == 3)
            wl_frm.frame_data.mimo_info.type = MIMO_TYPE_LDPC;
        else
            wl_frm.frame_data.mimo_info.type = MIMO_TYPE_NORMAL;

        wl_frm.frame_data.mimo_info.mcs = rx_info->mcs;
        snif_cb(&wl_frm,wl_frm.frame_data.mimo_info.len);
    } else
#endif
    {
//        printf("len:%04x\n", len);
        snif_cb((uint8_t *)buf,(uint16_t)len);
    }
}

/***********************************************************
*  Function: hwl_wf_sniffer_set
*  Input: en cb
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_sniffer_set(const bool en, const SNIFFER_CALLBACK cb)
{
    if((en) && (NULL == cb)) {
        return OPRT_INVALID_PARM;
    }
    
    int ret = 0;
    if(en) {
        ret = wifi_retransmit_packet_filter(1, 3);          //3ms
        if(0 != ret) {
            return OPRT_COM_ERROR;
        }
        snif_cb = cb;
        ret = wifi_set_promisc(RTW_PROMISC_ENABLE_4,__hwl_promisc_callback,1);
        if(0 != ret) {
            snif_cb = NULL;
            return OPRT_COM_ERROR;
        }
    }else {
        ret = wifi_retransmit_packet_filter(0, 0);
        if(0 != ret) {
            return OPRT_COM_ERROR;
        }

        ret = wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0);
        if(0 != ret) {
            return OPRT_COM_ERROR;
        }
        snif_cb = NULL;
    }
    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_get_ip
*  Input: wf
*  Output: ip
*  Return: int
***********************************************************/
int tuya_hal_wifi_get_ip(const WF_IF_E wf, NW_IP_S *ip)
{
    WF_WK_MD_E mode = 0;

    tuya_hal_wifi_get_work_mode(&mode);
    if((WF_STATION == wf) && \
       (mode != WWM_STATION && mode != WWM_STATIONAP)) {
        return OPRT_COM_ERROR;
    }else if((WF_AP == wf) && \
             (mode != WWM_SOFTAP && mode != WWM_STATIONAP)) {
        return OPRT_COM_ERROR;
    }

    uint8_t net_if_idx = 0;
    rtw_interface_t interface = 0;

    if(WF_STATION == wf) {
        net_if_idx = NETIF_STA_IDX;
        interface = RTW_STA_INTERFACE;
    }else {
        net_if_idx = NETIF_STA_IDX;
        if(WWM_STATIONAP == mode) {
            net_if_idx = NETIF_AP_IDX;
        }
        interface = RTW_AP_INTERFACE;
    }

    int ret = 0;
    ret = wifi_is_up(interface);
    if(1 != ret) {
        LOGE("net_if_idx:%d wifi_is_up err:%d\n",net_if_idx,ret);
        return OPRT_OK;
    }

    if(WF_STATION == wf) {
        WF_STATION_STAT_E stat = 0;
        tuya_hal_wifi_station_get_status(&stat);

        if(stat != WSS_GOT_IP) {
            LOGE("station mode do't get ip\n");
            return OPRT_COM_ERROR;
        }
    }

    uint32_t ip_address = 0;

    // ip
    ip_address = xnetif[net_if_idx].ip_addr.addr;
    sprintf(ip->ip,"%d.%d.%d.%d",(uint8_t)(ip_address),(uint8_t)(ip_address >> 8),\
                                 (uint8_t)(ip_address >> 16),(uint8_t)(ip_address >> 24));

    // gw
    ip_address = xnetif[net_if_idx].gw.addr;
    sprintf(ip->gw,"%d.%d.%d.%d",(uint8_t)(ip_address),(uint8_t)(ip_address >> 8),\
                                 (uint8_t)(ip_address >> 16),(uint8_t)(ip_address >> 24));

    // submask
    ip_address = xnetif[net_if_idx].netmask.addr;
    sprintf(ip->mask,"%d.%d.%d.%d",(uint8_t)(ip_address),(uint8_t)(ip_address >> 8),\
                                   (uint8_t)(ip_address >> 16),(uint8_t)(ip_address >> 24));

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_set_ip
*  Input: wf
*  Output: ip
*  Return: int
***********************************************************/
int tuya_hal_wifi_set_ip(const WF_IF_E wf, const NW_IP_S *ip)
{
    return OPRT_NOT_SUPPORTED;
}

static unsigned char hwl_wf_base16_decode(char code)
{
    if ('0' <= code && code <= '9') {
        return code - '0';
    } else if ('a' <= code && code <= 'f') {
        return code - 'a' + 10;
    } else if ('A' <= code && code <= 'F') {
        return code - 'A' + 10;
    } else {
        return 0;
    }
}

/***********************************************************
*  Function: hwl_wf_get_mac
*  Input: wf
*  Output: mac
*  Return: int
***********************************************************/
int tuya_hal_wifi_get_mac(const WF_IF_E wf, NW_MAC_S *mac)
{
#if 1
    char buf[20+1];

    int ret = 0;
    ret = wifi_get_mac_address(buf);
    if(ret != 0) {
        LOGE("wifi_get_mac_address ret：%d\n",ret);
        return OPRT_COM_ERROR;
    }
    // LOGD("read wifi mac:%s\n",buf);

    // aa:bb:cc:dd:ee:ff
    #if 1
    unsigned char l4,h4;
    int i = 0;
    int offset = 0;
    while (buf[offset] && i < sizeof(NW_MAC_S)) {
        h4 = hwl_wf_base16_decode(buf[offset]);
        l4 = hwl_wf_base16_decode(buf[offset+1]);
        mac->mac[i++] = (h4<<4)+l4;
        offset += 2;
        offset++; // skip :
    }
    #else
    sscanf(buf,"%02x:%02x:%02x:%02x:%02x:%02x",\
           &(mac->mac[0]),&(mac->mac[1]),&(mac->mac[2]),\
           &(mac->mac[3]),&(mac->mac[4]),&(mac->mac[5]));
    #endif
    if(wf == WF_AP){
        mac->mac[5] += 0x01;
    }
#else
    unsigned char *tmp = LwIP_GetMAC(&xnetif[0]);
    if(tmp == NULL) {
        return OPRT_COM_ERROR;
    }
    
    memcpy(mac->mac, tmp, 6);
    if(wf == 1)
        mac->mac[5] += 0x01;
#endif
    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_set_mac
*  Input: wf mac
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_set_mac(const WF_IF_E wf, const NW_MAC_S *mac)
{
    char buf[20+1];
    memset(buf,0,sizeof(buf));
    
    sprintf(buf,"%02x%02x%02x%02x%02x%02x",\
            mac->mac[0],mac->mac[1],mac->mac[2],\
            mac->mac[3],mac->mac[4],mac->mac[5]);
    LOGD("write wifi mac:%s\n",buf);

    int ret = 0;
    ret = wifi_set_mac_address(buf);
    if(ret != 0) {
        LOGE("wifi_set_mac_address ret：%d\n",ret);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_wk_mode_set
*  Input: mode
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_set_work_mode(const WF_WK_MD_E mode)
{
    int ret = 0;
    if((mode < WWM_LOWPOWER) || (mode > WWM_STATIONAP)) {
        LOGN("mode is out of range mode %d\n",mode);
        return OPRT_INVALID_PARM;
    }

    if((WWM_LOWPOWER == wf_mode) && (mode != WWM_LOWPOWER)) {
        ret = wifi_rf_on();
        if(ret != 0) {
            LOGE("wifi_rf_on err:%d\n",ret);
            //return OPRT_COM_ERROR; //影响低功耗模式wifi扫描
        }
    }
    vTaskDelay(20);
    pmu_acquire_wakelock(PMU_OS);

    ret = wifi_off();
    if(ret != 0) {
        LOGE("wifi off err:%d\n",ret);
        return OPRT_COM_ERROR;
    }

    vTaskDelay(20);
    switch(mode) {
        case WWM_LOWPOWER : {
            ret = wifi_rf_off();
        }
        break;

        case WWM_SNIFFER: {
            ret = wifi_on(RTW_MODE_PROMISC);
        }
        break;

        case WWM_STATION: {
            ret = wifi_on(RTW_MODE_STA);

            if(tuya_hal_get_lp_mode()) {
                pmu_release_wakelock(PMU_OS); //release wakelock
            }
        }
        break;

        case WWM_SOFTAP: {
            ret = wifi_on(RTW_MODE_AP);
            wext_set_sta_num(g_ap_num);
        }
        break;

        case WWM_STATIONAP: {
            ret = wifi_on(RTW_MODE_STA_AP);
            wext_set_sta_num(g_ap_num);
        }
        break;
    }

    if(ret != 0) {
        ret = wifi_off();
        ret |= wifi_rf_off();
        if(ret != 0) {
            //LOGE("set wifi mode error:%d\n",ret);
        }
        wf_mode = WWM_LOWPOWER;
        return OPRT_COM_ERROR;
    }
    wf_mode = mode;

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_close_concurrent_ap
*  Input: none
*  Output: mode
*  Return: OPERATE_RET
***********************************************************/
int tuya_hal_wifi_close_concurrent_ap(void)
{
    WF_WK_MD_E mode = WWM_LOWPOWER;
    tuya_hal_wifi_get_work_mode(&mode);
    if(mode == WWM_STATIONAP) {
        wifi_suspend_softap();
    }
    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_wk_mode_get
*  Input: none
*  Output: mode
*  Return: int
***********************************************************/
int tuya_hal_wifi_get_work_mode(WF_WK_MD_E *mode)
{
    *mode = wf_mode;
    return OPRT_OK;
}

static int find_ap_from_scan_buf(char*buf, int buflen, char *target_ssid, void *user_data)
{
    rtw_wifi_setting_t *pwifi = (rtw_wifi_setting_t *)user_data;
    int plen = 0;

    while(plen < buflen){
        u8 len, ssid_len, security_mode;
        char *ssid;

        // len offset = 0
        len = (int)*(buf + plen);
        // check end
        if(len == 0) break;
        // ssid offset = 14
        ssid_len = len - 14;
        ssid = buf + plen + 14 ;
        if((ssid_len == strlen(target_ssid)) && (!memcmp(ssid, target_ssid, ssid_len))) {
            strcpy((char*)pwifi->ssid, target_ssid);
            // channel offset = 13
            pwifi->channel = *(buf + plen + 13);
            // security_mode offset = 11
            security_mode = (u8)*(buf + plen + 11);
            if(security_mode == IW_ENCODE_ALG_NONE)
                pwifi->security_type = RTW_SECURITY_OPEN;
            else if(security_mode == IW_ENCODE_ALG_WEP)
                pwifi->security_type = RTW_SECURITY_WEP_PSK;
            else if(security_mode == IW_ENCODE_ALG_CCMP)
                pwifi->security_type = RTW_SECURITY_WPA2_AES_PSK;
            break;
        }
        plen += len;
    }
    return 0;
}

int tuya_hal_wifi_get_bssid(unsigned char *mac)
{
    if(NULL == mac) {
        return OPRT_INVALID_PARM;
    }
    
    int ret = wifi_get_ap_bssid(mac);
    if(ret < 0) {
        return OPRT_COM_ERROR;
    }
    
    return OPRT_OK;
}


static int get_ap_security_mode(char * ssid, rtw_security_t *security_mode)
{
    rtw_wifi_setting_t wifi;
    unsigned int scan_buflen = 1000;
    memset(&wifi, 0, sizeof(wifi));

    if(wifi_scan_networks_with_ssid(find_ap_from_scan_buf, (void*)&wifi, scan_buflen, ssid, strlen(ssid)) != RTW_SUCCESS){
        return 0;
    }

    if(strcmp(wifi.ssid, ssid) == 0){
        *security_mode = wifi.security_type;
        return 1;
    }

    return 0;
}

#if 0
//#if defined(ENABLE_AP_FAST_CONNECT) && (ENABLE_AP_FAST_CONNECT==1)
/***********************************************************
*  Function: hwl_wf_fast_station_connect
*  Input: none
*  Output: fast_ap_info
*  Return: int
***********************************************************/
int hwl_wf_get_connected_ap_info(FAST_WF_CONNECTED_AP_INFO_S *fast_ap_info)
{

    memset(fast_ap_info, 0, sizeof(FAST_WF_CONNECTED_AP_INFO_S));

    rtw_wifi_setting_t setting;
    if(wifi_get_setting((const char*)WLAN0_NAME,&setting) || setting.mode == RTW_MODE_AP) {
        return OPRT_COM_ERROR;
    }
    
    fast_ap_info->chan = setting.channel;
    fast_ap_info->security = setting.security_type;

    memcpy(fast_ap_info->ssid, tuya_ssid, strlen(tuya_ssid)+1);
    if (strlen(psk_passphrase64) == 64) {
        memcpy(fast_ap_info->passwd, psk_passphrase64, sizeof(fast_ap_info->passwd));
    }else if (strlen(psk_passphrase) != 0){
        memcpy(fast_ap_info->passwd, psk_passphrase[0], sizeof(fast_ap_info->passwd));
    }else {
        memcpy(fast_ap_info->passwd, tuya_pwd, sizeof(tuya_pwd));
    }

    LOGN("ssid %s,passwd:%s,sec_type:%d,chan:%d\n",fast_ap_info->ssid,fast_ap_info->passwd,fast_ap_info->security,fast_ap_info->chan);
    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_fast_station_connect
*  Input: ssid passwd
*  Output: mode
*  Return: int
***********************************************************/
int hwl_wf_fast_station_connect(FAST_WF_CONNECTED_AP_INFO_S *fast_ap_info)
{
    //LOGN("ssid: %s,passwd:%s,sec_type: %d,chan:%d\n",fast_ap_info->ssid,fast_ap_info->passwd,fast_ap_info->security,fast_ap_info->chan);
    uint8_t channel = fast_ap_info->chan;
    uint8_t  pscan_config = PSCAN_ENABLE | PSCAN_FAST_SURVEY;
    //set partial scan for entering to listen beacon quickly
    int ret = wifi_set_pscan_chan((uint8_t *)&channel, &pscan_config, 1);
    if(ret < 0){
        return OPRT_COM_ERROR;
    }

    int key_id = -1;
    if(RTW_SECURITY_WEP_PSK == fast_ap_info->security) {
        key_id = 0;
    }

    memset(tuya_ssid,0,sizeof(tuya_ssid));
    strcpy(tuya_ssid,fast_ap_info->ssid);

    memset(tuya_pwd,0,sizeof(tuya_pwd));
    strcpy(tuya_pwd,fast_ap_info->passwd);

    wifi_set_autoreconnect(1);
    ret = wifi_connect((char*)fast_ap_info->ssid, fast_ap_info->security, (char*)fast_ap_info->passwd, strlen(fast_ap_info->ssid),
            strlen(fast_ap_info->passwd), key_id, NULL);
    if(ret == RTW_SUCCESS){
        LwIP_DHCP(0, DHCP_START);
    }

    return OPRT_OK;

}
#endif
/***********************************************************
*  Function: tuya_hal_wifi_get_connected_ap_info_v2
*  Input:    none
*  Output:   fast_ap_info
*  Return: int
***********************************************************/
int tuya_hal_wifi_get_connected_ap_info_v2(void **fast_ap_info)
{
    if(NULL == fast_ap_info) {
        return OPRT_INVALID_PARM;
    }

    FAST_WF_CONNECTED_AP_INFO_V2_S *ap_infor_v2_buf = NULL;
    unsigned int len = sizeof(FAST_WF_CONNECTED_AP_INFO_S);
    ap_infor_v2_buf = (FAST_WF_CONNECTED_AP_INFO_V2_S *)tuya_hal_system_malloc(sizeof(FAST_WF_CONNECTED_AP_INFO_V2_S)+len);
    if(NULL == ap_infor_v2_buf) {
        return OPRT_MALLOC_FAILED;
    }
    memset(ap_infor_v2_buf, 0, sizeof(FAST_WF_CONNECTED_AP_INFO_V2_S)+len);

    rtw_wifi_setting_t setting;
    if(wifi_get_setting((const char*)WLAN0_NAME,&setting) || setting.mode == RTW_MODE_AP) {
        return OPRT_COM_ERROR;
    }
    
    FAST_WF_CONNECTED_AP_INFO_S ap_info = {0};
    ap_info.chan = setting.channel;
    ap_info.security = setting.security_type;

    memcpy(ap_info.ssid, tuya_ssid, strlen(tuya_ssid)+1);
    if (strlen(psk_passphrase64) == 64) {
        memcpy(ap_info.passwd, psk_passphrase64, sizeof(ap_info.passwd));
    }else if (strlen(psk_passphrase) != 0){
        memcpy(ap_info.passwd, psk_passphrase[0], sizeof(ap_info.passwd));
    }else {
        memcpy(ap_info.passwd, tuya_pwd, sizeof(tuya_pwd));
    }

    ap_infor_v2_buf->len = len;
    memcpy(ap_infor_v2_buf->data, &ap_info, len);

    *fast_ap_info = (void *)ap_infor_v2_buf;

    return OPRT_OK;
}

/***********************************************************
*  Function: tuya_hal_fast_station_connect_v2
*  Input:    fast_ap_info
*  Output:   none
*  Return:   int
***********************************************************/
int tuya_hal_fast_station_connect_v2( FAST_WF_CONNECTED_AP_INFO_V2_S *fast_ap_info)
{
    if(NULL == fast_ap_info) {
        return OPRT_INVALID_PARM;
    }

    FAST_WF_CONNECTED_AP_INFO_S *ap_info = (FAST_WF_CONNECTED_AP_INFO_S *)fast_ap_info->data;

    unsigned char channel = ap_info->chan;
    unsigned char pscan_config = PSCAN_ENABLE | PSCAN_FAST_SURVEY;
    //set partial scan for entering to listen beacon quickly
    int ret = wifi_set_pscan_chan((uint8_t *)&channel, &pscan_config, 1);
    if(ret < 0){
        return OPRT_COM_ERROR;
    }

    int key_id = -1;
    if(RTW_SECURITY_WEP_PSK == ap_info->security) {
        key_id = 0;
    }

    memset(tuya_ssid,0,sizeof(tuya_ssid));
    strcpy(tuya_ssid,ap_info->ssid);

    memset(tuya_pwd,0,sizeof(tuya_pwd));
    strcpy(tuya_pwd,ap_info->passwd);

    wifi_set_autoreconnect(1);
    ret = wifi_connect((char*)ap_info->ssid, ap_info->security, (char*)ap_info->passwd, strlen(ap_info->ssid),strlen(ap_info->passwd), key_id, NULL);
    if(ret == RTW_SUCCESS){
        LwIP_DHCP(0, DHCP_START);
    }

    return OPRT_OK;
}
//#endif

/***********************************************************
*  Function: hwl_wf_station_connect
*  Input: ssid passwd
*  Output: mode
*  Return: int
***********************************************************/
// only support wap/wap2 
int tuya_hal_wifi_station_connect(const char *ssid, const char *passwd)
{
    int op_ret = OPRT_OK;
    rtw_security_t security_type_tmp = RTW_SECURITY_WPA2_AES_PSK;
    if(1 != get_ap_security_mode(ssid, &security_type_tmp)) {
        if(1 != get_ap_security_mode(ssid, &security_type_tmp)) {//scan once more
            ap_not_find = TRUE;
            return OPRT_ROUTER_NOT_FIND;
        }
    }
    ap_not_find = FALSE;
    rtw_security_t	security_type = RTW_SECURITY_WPA2_AES_PSK;

    int passwd_len = 0;
    if(passwd) {
        passwd_len = strlen(passwd);
    }

    if(0 == passwd_len) {
        security_type = RTW_SECURITY_OPEN;
    }else if(5 == passwd_len) {
        security_type = RTW_SECURITY_WEP_PSK;
    }else{ // if(13 == passwd_len) 
        #if 0
        if(1 != get_ap_security_mode(ssid, &security_type)) {
            LOGE("get_ap_security_mode error\n");
        }
        #endif
        security_type = security_type_tmp;
    }

    int mode = 0;
    
    wext_get_mode(WLAN0_NAME, &mode);
    if(mode == IW_MODE_MASTER) {
        #if CONFIG_LWIP_LAYER
        dhcps_deinit();
        #endif
        //wifi_off();
        //vTaskDelay(20);
        //if (wifi_on(RTW_MODE_STA) < 0) {
        //    LOGE("ERROR: Wifi on failed!\n");
        //    return OPRT_COM_ERROR;
        //}
    }

    tuya_hal_wifi_station_disconnect();
    wifi_set_autoreconnect(1);

    int key_id = -1;
    if(RTW_SECURITY_WEP_PSK == security_type) {
        key_id = 0;
    }

    memset(tuya_ssid,0,sizeof(tuya_ssid));
    strcpy(tuya_ssid,ssid);

    memset(tuya_pwd,0,sizeof(tuya_pwd));
    strcpy(tuya_pwd,passwd);

    int ret = 0;
	LOGD("check parameters:SSID:%s,security_type:%d,passwd:%s,keyId:%d\n",ssid,security_type,passwd,key_id);
    ret = wifi_connect(ssid, security_type, passwd, strlen(ssid),
                       passwd_len, key_id, NULL);
    if(0 != ret) {
         LOGE("ERROR: wifi_connect:%d\n",ret);
         //return OPRT_COM_ERROR;
    }else{
		LOGE("OK: wifi_connect:%d\n",ret);
	} 

#if CONFIG_LWIP_LAYER
#if 1
    if(NULL == dhcp_thrd) {
        op_ret = tuya_hal_thread_create(&dhcp_thrd, "dhcp_thread", 1024+1024, 5, __dhcp_thread, NULL);
        if(OPRT_OK != op_ret) {
            LOGE("CreateAndStart error, ret:%d\n",op_ret);
            return op_ret;
        }else{
			LOGE("CreateAndStart ok\n");
		}
    }
#else
    LwIP_DHCP(NETIF_STA_IDX, DHCP_START);
#endif

#endif

    return OPRT_OK;
}

static void __dhcp_thread(void* pArg)
{
    uint8_t ret = 0;
    int remain = 5;

    while(remain) {
        ret = LwIP_DHCP(NETIF_STA_IDX, DHCP_START);
        if(ret == DHCP_ADDRESS_ASSIGNED) {
            break;
        }
        remain--;
    }

    if(0 == remain) {
        LOGE("dhcp error\n");
    }
	
	
	LOGE("__dhcp_thread delete********,ret:%d,dhcp_thrd:0x%x\n",ret,dhcp_thrd);
	THREAD_HANDLE thread_handle = dhcp_thrd; 
	dhcp_thrd = NULL;		
	ret = tuya_hal_thread_release(thread_handle);
	if(ret != OPRT_OK){
		LOGE("tuya_hal_thread_release err:%d\n",ret);
	}else{
		dhcp_thrd = NULL;		
		LOGE("__dhcp_thread delete********,ret:%d,dhcp_thrd:0x%x\n",ret,dhcp_thrd);
	}
}

/***********************************************************
*  Function: hwl_wf_station_disconnect
*  Input: none
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_station_disconnect(void)
{
    int ret = 0;
    ret = wifi_disconnect();
    if(ret != 0) {
        LOGE("wifi_disconnect err:%d\n",ret);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_station_get_conn_ap_rssi
*  Input: none
*  Output: rssi
*  Return: int
***********************************************************/
int tuya_hal_wifi_station_get_conn_ap_rssi(int8_t *rssi)
{
    int ret = 0;
    int tmp_rssi = 0;
    
    ret = wifi_get_rssi(&tmp_rssi);
    if(ret < 0) {
        return OPRT_COM_ERROR;
    }

    *rssi = tmp_rssi;

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_station_stat_get
*  Input: none
*  Output: stat
*  Return: int
***********************************************************/
int tuya_hal_wifi_station_get_status(WF_STATION_STAT_E *stat)
{
    extern int error_flag;

    int ret = 0;

    ret = wifi_is_connected_to_ap();
    if(RTW_SUCCESS != ret) {
        switch (error_flag) {
        case RTW_NO_ERROR:                  *stat = WSS_IDLE;       break;
        case RTW_NONE_NETWORK:              *stat = WSS_NO_AP_FOUND; break;
        case RTW_CONNECT_FAIL:              *stat = WSS_CONN_FAIL;  break;
        case RTW_WRONG_PASSWORD:            *stat = WSS_PASSWD_WRONG; break;
        case RTW_4WAY_HANDSHAKE_TIMEOUT:    *stat = WSS_CONN_FAIL;  break;
        case RTW_DHCP_FAIL:                 *stat = WSS_CONN_FAIL;  break;
        case RTW_UNKNOWN:                   *stat = WSS_IDLE;       break;
        }

        if((*stat == WSS_IDLE) && (ap_not_find == TRUE)) {
            *stat = WSS_NO_AP_FOUND;
        }
        return OPRT_OK;
    }

    if(0 == (xnetif[NETIF_STA_IDX].flags & NETIF_FLAG_UP)) {
        *stat = WSS_CONN_SUCCESS;
        return OPRT_OK;
    }

    if(0 == (xnetif[NETIF_STA_IDX].flags & NETIF_FLAG_DHCP)) {
        *stat = WSS_CONN_SUCCESS;
        return OPRT_OK;
    }

    if (xnetif[NETIF_STA_IDX].ip_addr.addr == INADDR_ANY) {
        *stat = WSS_CONN_SUCCESS;
        return OPRT_OK;
    }

    *stat = WSS_GOT_IP;

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_ap_start
*  Input: cfg
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_ap_start(const WF_AP_CFG_IF_S *cfg)
{
    if(NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    WF_WK_MD_E mode = 0;
    tuya_hal_wifi_get_work_mode(&mode);
    if(WWM_SOFTAP != mode && WWM_STATIONAP != mode) {
        LOGE("wifi mode:%d\n",mode);
        return OPRT_COM_ERROR;
    }

    rtw_security_t security_type = 0;
    if(cfg && 0 == cfg->passwd[0]) {
        security_type = RTW_SECURITY_OPEN;
    }else {
        if(WAAM_OPEN == cfg->md) {
            security_type = RTW_SECURITY_OPEN;
        }else if(WAAM_WEP == cfg->md) {
            security_type = RTW_SECURITY_WEP_SHARED;
        }else if(WAAM_WPA_PSK == cfg->md) {
            security_type = RTW_SECURITY_WPA_AES_PSK;
        }else if(WAAM_WPA2_PSK == cfg->md) {
            security_type = RTW_SECURITY_WPA2_AES_PSK;
        }else {
            security_type = RTW_SECURITY_WPA_WPA2_MIXED;
        }
    }

    int ret = 0;
	LOGD("ap start chan:%d...",cfg->chan);
    ret = wifi_start_ap((char *)cfg->ssid,security_type, \
                         (char *)cfg->passwd,strlen((char *)cfg->ssid), \
                         strlen((char *)cfg->passwd),cfg->chan); // cfg->chan
    if(0 != ret) {
        LOGE("wifi_start_ap err:%d, chan:%d\n",ret,cfg->chan);
        return OPRT_COM_ERROR;
    }
	LOGD("ap end...");

    UINT index = NETIF_STA_IDX;
    if(mode == WWM_STATIONAP) {
        index = NETIF_AP_IDX;
    }

    #if defined(ENABLE_LAN_ENCRYPTION) && (ENABLE_LAN_ENCRYPTION==1)
    IP4_ADDR(&xnetif[index].ip_addr, 192, 168, 175, 1);
    IP4_ADDR(&xnetif[index].gw, 192, 168, 175, 1);
    IP4_ADDR(&xnetif[index].netmask, 255, 255, 255, 0);
    #endif
    
    dhcps_init(&xnetif[index]);

    return OPRT_OK;
}

/***********************************************************
*  Function: hwl_wf_ap_stop
*  Input: none
*  Output: none
*  Return: int
***********************************************************/
int tuya_hal_wifi_ap_stop(void)
{
    dhcps_deinit();
    tuya_hal_wifi_set_work_mode(WWM_STATION);

    return OPRT_OK;
}


static uint8_t s_country_num[8] = "CN";
int tuya_hal_wifi_set_country_code(const char *p_country_code)
{
    strcpy(s_country_num, p_country_code);
    return OPRT_OK;
}

void hwl_wf_get_country_code(unsigned char *country_code)
{
    strcpy(country_code, s_country_num);
}

static uint32_t lp_rcnt = 0;

int tuya_hal_wifi_lowpower_disable(void)
{
    if(!tuya_hal_get_lp_mode()) {
        //LOGE("it's in normal mode\n");
        return OPRT_COM_ERROR;
    }
    
    if(!lp_rcnt++) {
        pmu_acquire_wakelock(PMU_DEV_USER_BASE); //acquire wakelock
    }

    return OPRT_OK;
}

int tuya_hal_wifi_lowpower_enable(void)
{
    if(!tuya_hal_get_lp_mode()) {
        return OPRT_COM_ERROR;
    }

    if(lp_rcnt > 0) {
        lp_rcnt--;
    }
    
    if(!lp_rcnt) {
        pmu_release_wakelock(PMU_DEV_USER_BASE);
    }
    
    return OPRT_OK;
}

int tuya_hal_wifi_send_mgnt(const uint8_t *buf, const uint32_t len)
{
    int ret = wext_send_mgnt(WLAN0_NAME, buf, len, 1);
    if(ret < 0) {
        return OPRT_COM_ERROR;
    }
    
    return OPRT_OK;
}

static WIFI_REV_MGNT_CB mgnt_recv_cb = NULL;
void static __rtw_event_handler_t(char *buf, int buf_len, int flags, void* handler_user_data)
{
     PROBE_REQUEST_FIX_S *req_fix_head = (PROBE_REQUEST_FIX_S*)buf;
     if(req_fix_head->pack_head.type_and_subtype != PROBE_REQUEST_TYPE_SUBTYPE) {
        return;
     }

    // printf("len:%d, rssi:%d\n", buf_len, (char)flags);
    // for (uint32_t i = 0; i < buf_len; i++) {
    //     printf("%02x, ", (unsigned char)buf[i]);
    //     if ((i + 1) % 8 == 0) {
    //         printf("  ");
    //     }
    //     if ((i + 1) % 16 == 0) {
    //         printf("\n");
    //     }
    // }
    // printf("\n\n");

//     NW_MAC_S devmac;
//     int op_ret = hwl_wf_get_mac(0,&devmac);
//     if((req_head->addr1 != BROADCAST_MAC_ADDR) && \
//        (memcmp(req_head->addr1,devmac.mac,6))) {
//         return;
//     }

    if((req_fix_head->tag_ssid.index != TAG_SSID_NUMBER) || \
       (req_fix_head->tag_ssid.len != strlen(PROBE_SSID)) || \
       memcmp(req_fix_head->tag_ssid.ptr,PROBE_SSID,strlen(PROBE_SSID))) {
        return;
    }

    BEACON_TAG_DATA_UNIT_S *tag_data = (BEACON_TAG_DATA_UNIT_S *)(buf + sizeof(PROBE_REQUEST_FIX_S) + strlen(PROBE_SSID));
    if((tag_data->index != TAG_PAYLOAD_NUMBER) || \
       (tag_data->len > PROBE_REQUEST_PAYLOAD_LEN_MAX)) {
        return;
    }

    NW_MAC_S srcmac, dstmac;
    memcpy(srcmac.mac, req_fix_head->pack_head.addr2, 6);
    memcpy(dstmac.mac, req_fix_head->pack_head.addr1, 6);

    mgnt_recv_cb(tag_data->ptr,tag_data->len,&srcmac,&dstmac);
    return;
}

int tuya_hal_wifi_register_recv_mgnt_callback(bool enable, WIFI_REV_MGNT_CB recv_cb)
{
    if(enable) {
        WF_WK_MD_E mode;
        int ret = hwl_wf_wk_mode_get(&mode);
        if(OPRT_OK != ret) {
            return OPRT_COM_ERROR;
        }

        if((mode == WWM_LOWPOWER) || (mode == WWM_SNIFFER)) {
            return OPRT_COM_ERROR;
        }

        wifi_set_indicate_mgnt(1);
        wifi_reg_event_handler(WIFI_EVENT_RX_MGNT,__rtw_event_handler_t,NULL);
        mgnt_recv_cb = recv_cb;

    }else {
        wifi_set_indicate_mgnt(0);
        wifi_reg_event_handler(WIFI_EVENT_RX_MGNT,NULL,NULL);
        mgnt_recv_cb = NULL;
    }
}

int tuya_hal_wifi_set_socket_broadcast_all(const int socket_fd, const bool enable)
{
    extern struct netif xnetif[NET_IF_NUM];
    if(enable == TRUE) {
        xnetif[1].flags |= NETIF_FLAG_BROADCAST;
    } else {
        xnetif[1].flags &= ~NETIF_FLAG_BROADCAST;
    }
    return OPRT_OK;
}

/***********************************************************
*  Function: tuya_hal_wifi_set_ap_num
*  Input: num
*  Output:
*  Return: int
***********************************************************/
int tuya_hal_wifi_set_ap_num(const unsigned char num)
{
    if(num == 0)
        return -1;
    
    g_ap_num = num;
    return 0;
}
