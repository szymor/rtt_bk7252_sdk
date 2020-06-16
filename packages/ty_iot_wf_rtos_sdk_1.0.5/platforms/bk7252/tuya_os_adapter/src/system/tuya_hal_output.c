/***********************************************************
*  File: uni_output.c
*  Author: nzy
*  Date: 20170921
***********************************************************/
#define _UNI_OUTPUT_GLOBAL
#ifdef CONFIG_PLATFORM_8195A
#include "rtl8195a.h"
#endif

#ifdef CONFIG_PLATFORM_8711B
#include "rtl8710b.h"
#endif
#include "tuya_hal_system.h"
#include "tuya_hal_network.h"
#include "../errors_compat.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
/* 终端输出函数 */
#ifdef CONFIG_PLATFORM_8195A
    #define OutputPrint DBG_8195A
#endif

#ifdef CONFIG_PLATFORM_8711B
    #define OutputPrint DiagPrintf
#endif

/***********************************************************
*************************variable define********************
***********************************************************/

/***********************************************************
*************************function define********************
***********************************************************/

/***********************************************************
*  Function: uni_log_output 
*  Input: str
*  Output: none
*  Return: none
***********************************************************/
VOID tuya_hal_output_log(IN const char *str)
{
    OutputPrint("%s",str);
}

