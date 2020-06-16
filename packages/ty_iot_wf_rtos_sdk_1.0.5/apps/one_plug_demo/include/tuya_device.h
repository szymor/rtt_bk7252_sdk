/***********************************************************
*  File: tuya_device.h
*  Author: nzy
*  Date: 20170909
***********************************************************/
#ifndef _TUYA_DEVICE_H
#define _TUYA_DEVICE_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _TUYA_DEVICE_GLOBAL
    #define _TUYA_DEVICE_EXT 
#else
    #define _TUYA_DEVICE_EXT extern
#endif

/***********************************************************
*************************micro define***********************
***********************************************************/
#define PRODUCT_KEY "key43esxvyddvapc"
#define FIRMWAIRE_KEY PRODUCT_KEY
#define DEV_SW_VERSION USER_SW_VER

/***********************************************************
*************************variable define********************
***********************************************************/
typedef enum{
    DP_SWITCH = 1,
    DP_COUNT_DOWN = 9
}DPID_E;

//app控制上电状态，默认下次重启生效
#define RST_POWER_INIT
#define INIT_OFF  0  //上电关
#define INIT_ON   1  //上电开
#define INIT_MEM  2  //断电记忆

#define RELAY_ACT_INTER           500u //500ms

/***********************************************************
*************************function define********************
***********************************************************/
/***********************************************************
*  Function: pre_device_init
*  Input: none
*  Output: none
*  Return: none
*  Note: to initialize device before device_init
***********************************************************/
_TUYA_DEVICE_EXT \
VOID pre_device_init(VOID);

/***********************************************************
*  Function: device_init 
*  Input: none
*  Output: none
*  Return: none
***********************************************************/
_TUYA_DEVICE_EXT \
OPERATE_RET device_init(VOID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
