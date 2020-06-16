/***********************************************************
*  File: uni_storge.c 
*  Author: nzy
*  Date: 20170920
***********************************************************/
#define __UNI_STORGE_GLOBALS
#include "flash_api.h"
#ifdef CONFIG_PLATFORM_8195A
    #include "rtl8195a.h"
#endif
#ifdef CONFIG_PLATFORM_8711B
    #include "rtl8710b.h"
#endif
#include <device_lock.h>
#include <basic_types.h>
#include "tuya_hal_storge.h"
#include "../errors_compat.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
#define PARTITION_SIZE (1 << 12) /* 4KB */
#define FLH_BLOCK_SZ PARTITION_SIZE

// flash map   0x000F5000  0x000FD000   0x8000
#ifdef CONFIG_PLATFORM_8711B

#if OPERATING_SYSTEM == SYSTEM_REALTEK8710_1M

#define SIMPLE_FLASH_START 0x000F5000
#define SIMPLE_FLASH_SIZE 0x8000 // 32K

#define SIMPLE_FLASH_SWAP_START 0x000FD000
#define SIMPLE_FLASH_SWAP_SIZE 0x2000 // 8K

#endif


#if OPERATING_SYSTEM == SYSTEM_REALTEK8710_2M

#define SIMPLE_FLASH_START (0x200000 - 0x10000 - 0x4000)
#define SIMPLE_FLASH_SIZE 0x10000 // 64k

#define SIMPLE_FLASH_SWAP_START (0x200000 - 0x4000)
#define SIMPLE_FLASH_SWAP_SIZE 0x4000 // 16k

#define SIMPLE_FLASH_KEY_ADDR  (0x200000 - 0x10000 - 0x4000 - 0x1000)


#endif

#else
    #ifdef CONFIG_PLATFORM_8195A
    #define SIMPLE_FLASH_START 0x000F5000
    #define SIMPLE_FLASH_SIZE 0x8000 // 32K

    #define SIMPLE_FLASH_SWAP_START 0x000FD000
    #define SIMPLE_FLASH_SWAP_SIZE 0x2000 // 8K

    #define SIMPLE_FLASH_KEY_ADDR 0x000FF000
    #endif
#endif





/***********************************************************
*************************variable define********************
***********************************************************/
static flash_t obj;

static UNI_STORAGE_DESC_S storage = {
    SIMPLE_FLASH_START,
    SIMPLE_FLASH_SIZE,
    FLH_BLOCK_SZ,
    SIMPLE_FLASH_SWAP_START,
    SIMPLE_FLASH_SWAP_SIZE,
    SIMPLE_FLASH_KEY_ADDR
};

static UF_PARTITION_TABLE_S uf_file = {
    .sector_size = PARTITION_SIZE,
    .uf_partition_num = 2,
    .uf_partition = {
        {SIMPLE_FLASH_KEY_ADDR - 0x8000, 0x8000},
        {SIMPLE_FLASH_KEY_ADDR - 0x8000 - 0x18000, 0x18000}
    }
};

/***********************************************************
*************************function define********************
***********************************************************/
/***********************************************************
*  Function: uni_flash_read
*  Input: addr size
*  Output: dst
*  Return: none
***********************************************************/
int tuya_hal_flash_read(IN const uint32_t addr, OUT uint8_t *dst, IN const uint32_t size)
{
    if(NULL == dst) {
        return OPRT_INVALID_PARM;
    }

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_stream_read(&obj, addr, size, dst);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    return OPRT_OK;
}

/***********************************************************
*  Function: uni_flash_write
*  Input: addr src size
*  Output: none
*  Return: none
***********************************************************/
int tuya_hal_flash_write(IN const uint32_t addr, IN const uint8_t *src, IN const uint32_t size)
{
    if(NULL == src) {
        return OPRT_INVALID_PARM;
    }

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_stream_write(&obj, addr, size, src);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    return OPRT_OK;
}

/***********************************************************
*  Function: uni_flash_erase
*  Input: addr size
*  Output: 
*  Return: none
***********************************************************/
int tuya_hal_flash_erase(IN const uint32_t addr, IN const uint32_t size)
{
    uint16_t start_sec = (addr/PARTITION_SIZE);
    uint16_t end_sec = ((addr+size-1)/PARTITION_SIZE);

    int i = 0;
    for(i = start_sec;i <= end_sec;i++) {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&obj, PARTITION_SIZE*i);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }

    return OPRT_OK;
}
UF_PARTITION_TABLE_S *tuya_hal_uf_get_desc(VOID)
{
    return &uf_file;
}

/***********************************************************
*  Function: uni_get_storge_desc
*  Input: none
*  Output: none
*  Return: UNI_STORGE_DESC_S
***********************************************************/
UNI_STORAGE_DESC_S *tuya_hal_storage_get_desc(VOID)
{
    return &storage;
}
