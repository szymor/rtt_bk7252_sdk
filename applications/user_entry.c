#include "rtthread.h"
#include "user_include.h"
#include "wlan_dev.h"

#define DATA_TEMPLATE_CLOCK_THREAD_STACK_SIZE 1024 * 8

static rt_sem_t wait_sem = RT_NULL;
static struct rt_wlan_info wlan_info;
user_status_data_t data_connect_ctx = { 0x00 };

static void wifi_connect_callback(struct rt_wlan_device *device, rt_wlan_event_t event, void *user_data)
{
    HAL_Printf("wifi_ocnnect_callback.\r\n");
    rt_sem_release(wait_sem);
}

user_status_data_t *user_get_connect_status(void)
{
    return &data_connect_ctx;
}

int clock_main( void )
{
    int ret = 0;
    rt_thread_t tid;
    int stack_size = DATA_TEMPLATE_CLOCK_THREAD_STACK_SIZE;
    struct rt_wlan_device *wlan;

    // wait for connect to router......
    wait_sem = rt_sem_create("sem_conn", 0, RT_IPC_FLAG_FIFO);

    user_get_connect_status()->connect_status = USER_WLAN_STA_INIT;

    /* bsp init  */
    // bsp_init();

    // rt_kprintf("after bsp init.\r\n");

    // connect to router
    /* get wlan device */
    wlan = (struct rt_wlan_device *)rt_device_find(WIFI_DEVICE_STA_NAME);
    if (!wlan)
    {
        rt_kprintf("no wlan:%s device\n", WIFI_DEVICE_STA_NAME);
        return 0;
    }
    rt_wlan_init(wlan, WIFI_STATION);
    ret = rt_wlan_register_event_handler(wlan, WIFI_EVT_STA_CONNECTED, wifi_connect_callback);
    if (0 != ret)
    {
        rt_kprintf("register event handler error!\r\n");
    }
    // ret = rt_wlan_register_event_handler(wlan, WIFI_EVT_LINK_UP, wifi_connect_callback);
    // if (0 != ret)
    // {
    //     rt_kprintf("register event handler error!\r\n");
    // }
    /* TODO: use easy-join to replace */
    rt_wlan_info_init(&wlan_info, WIFI_STATION, SECURITY_WPA2_MIXED_PSK, "Xiaomi_brown");
    rt_wlan_connect(wlan, &wlan_info, "12345678q");
    rt_wlan_info_deinit(&wlan_info);
    user_get_connect_status()->connect_status = USER_WLAN_STA_CONNECTING;

    rt_kprintf("start to connect ap ...\n");

    // wait until module connect to ap success
    ret = rt_sem_take(wait_sem, RT_WAITING_FOREVER);
    if (0 != ret)
    {
        rt_kprintf("wait_sem error!\r\n");
    }
    rt_kprintf("connect to ap success!\r\n");
    user_get_connect_status()->connect_status = USER_WLAN_STA_CONNECTED;
    rt_thread_mdelay(1000);

    tid = rt_thread_create("data_template_clock", (void (*)(void *))data_template_clock_thread,
                           NULL, stack_size, RT_THREAD_PRIORITY_MAX / 2 - 1, 10);

    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    }

    weather_task_start();

    return 0;
}
