/***********************************************************
*  File: uni_mutex.c
*  Author: nzy
*  Date: 120427
***********************************************************/
#define _UNI_MUTEX_GLOBAL
#include <rtthread.h>
#include "task.h"
#include "tuya_hal_system_internal.h"
#include "tuya_cloud_types.h"
#include "tuya_hal_mutex.h"
#include "../errors_compat.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
typedef rt_mutex_t THRD_MUTEX;

typedef struct
{
    THRD_MUTEX mutex;
}MUTEX_MANAGE,*P_MUTEX_MANAGE;

/***********************************************************
*************************variable define********************
***********************************************************/

/***********************************************************
*************************function define********************
***********************************************************/

/***********************************************************
*  Function: CreateMutexAndInit 创建一个互斥量并初始化
*  Input: none
*  Output: pMutexHandle->新建的互斥量句柄
*  Return: OPERATE_RET
*  Date: 120427
***********************************************************/
int tuya_hal_mutex_create_init(OUT MUTEX_HANDLE *pMutexHandle)
{
    if(!pMutexHandle)
        return OPRT_INVALID_PARM;
    
    P_MUTEX_MANAGE pMutexManage;
    pMutexManage = (P_MUTEX_MANAGE)tuya_hal_internal_malloc(sizeof(MUTEX_MANAGE));
    if(!(pMutexManage))
        return OPRT_MALLOC_FAILED;
    
    pMutexManage->mutex = rt_mutex_create("tuya_mutex", RT_IPC_FLAG_FIFO);

    if(NULL == pMutexManage->mutex) {
        return OPRT_INIT_MUTEX_FAILED;
    }

    *pMutexHandle = (MUTEX_HANDLE)pMutexManage;

    return OPRT_OK;
}

/***********************************************************
*  Function: MutexLock 加锁
*  Input: mutexHandle->互斥量句柄
*  Output: none
*  Return: OPERATE_RET
*  Date: 120427
***********************************************************/
int tuya_hal_mutex_lock(IN const MUTEX_HANDLE mutexHandle)
{
    if(!mutexHandle)
        return OPRT_INVALID_PARM;

    P_MUTEX_MANAGE pMutexManage;
    pMutexManage = (P_MUTEX_MANAGE)mutexHandle;
    
    rt_err_t ret;
    ret = rt_mutex_take (pMutexManage->mutex, RT_WAITING_FOREVER);
    if(RT_EOK != ret) {
        return OPRT_MUTEX_LOCK_FAILED;
    }

    return OPRT_OK;
}

/***********************************************************
*  Function: MutexUnLock 解锁
*  Input: mutexHandle->互斥量句柄
*  Output: none
*  Return: OPERATE_RET
*  Date: 120427
***********************************************************/
int tuya_hal_mutex_unlock(IN const MUTEX_HANDLE mutexHandle)
{
    if(!mutexHandle)
        return OPRT_INVALID_PARM;
    
    P_MUTEX_MANAGE pMutexManage;
    pMutexManage = (P_MUTEX_MANAGE)mutexHandle;
    
    rt_err_t ret; 
    ret = rt_mutex_release(pMutexManage->mutex);
    if(RT_EOK != ret) {
        return OPRT_MUTEX_UNLOCK_FAILED;
    }

    return OPRT_OK;
}

/***********************************************************
*  Function: ReleaseMutexManage 释放互斥锁管理结构资源
*  Input: mutexHandle->互斥量句柄
*  Output: none
*  Return: OPERATE_RET
*  Date: 120427
***********************************************************/
int tuya_hal_mutex_release(IN const MUTEX_HANDLE mutexHandle)
{
    if(!mutexHandle)
        return OPRT_INVALID_PARM;

    P_MUTEX_MANAGE pMutexManage;
    pMutexManage = (P_MUTEX_MANAGE)mutexHandle;
    
    rt_err_t ret; 
    ret = rt_mutex_detach(pMutexManage->mutex);

    tuya_hal_internal_free(mutexHandle);

    if(RT_EOK != ret) {
        return OPRT_MUTEX_RELEASE_FAILED;
    }
    return OPRT_OK;
}


