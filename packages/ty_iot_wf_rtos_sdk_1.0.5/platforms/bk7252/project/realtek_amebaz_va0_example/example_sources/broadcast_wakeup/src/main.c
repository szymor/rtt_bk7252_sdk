#include "device.h"
#include "gpio_api.h"   // mbed
#include "gpio_irq_api.h"   // mbed
#include "sleep_ex_api.h"
#include "sys_api.h"
#include "diag.h"
#include "main.h"
#include "serial_api.h"
#include "freertos_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include <platform/platform_stdlib.h>
#include <lwip/sockets.h>
#include <lwip_netconf.h>
#include <lwip/netif.h>
#include <wifi/wifi_ind.h>

#define UDP_SERVER_PORT  25000
#define MAX_RECV_SIZE    1500

static void example_udp_server(void)
{
    int                   server_fd;
    struct sockaddr_in   ser_addr , client_addr;
    int addrlen = sizeof(struct sockaddr_in);
    u32 recv_cnt = 0;
    char recv_data[MAX_RECV_SIZE];

    //create socket
    if((server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
        printf("\n\r[ERROR] Create socket failed\n");
        return;
    }
    printf("\n\r[INFO] Create socket successfully\n");

    //initialize structure dest
    memset(&ser_addr, 0, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(UDP_SERVER_PORT);
    ser_addr.sin_addr.s_addr= htonl(INADDR_ANY);

    //Assign a port number to socket
    if(bind(server_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr)) != 0){
        printf("\n\r[ERROR] Bind socket failed\n");
        closesocket(server_fd);
        return;
    }
    printf("\n\r[INFO] Bind socket successfully\n");

    while(1){
        memset(recv_data, 0, MAX_RECV_SIZE);
        if(recvfrom(server_fd, recv_data, MAX_RECV_SIZE, 0, (struct sockaddr *)&client_addr, &addrlen) > 0){
            recv_cnt++;
            printf("\n\r[INFO] Receive data : %s recv_cnt %d \r\n",recv_data, recv_cnt);
        }
    }

    closesocket(server_fd);

    return;
}
static void example_broadcast_wakeup_thread(void *param)
{
    vTaskDelay(1000);
    DBG_8195A("\r\nplease connect the AP.\r\ntickless will begin after 10s...\r\n");
    vTaskDelay(10000);
    DBG_8195A("example ipv6 BEGIN...\r\n");

    example_udp_server();

    vTaskDelete(NULL);
}
void example_broadcast_wakeup(void)
{
    if(xTaskCreate(example_broadcast_wakeup_thread, ((const char*)"example_broadcast_wakeup_thread"), 1024, NULL, tskIDLE_PRIORITY + 1 + PRIORITIE_OFFSET, NULL) != pdPASS)
        printf("\n\r%s xTaskCreate(example_broadcast_wakeup_thread) failed", __FUNCTION__);
}
void main(void)
{
    /* Initialize log uart and at command service */
    ReRegisterPlatformLogUart();

#if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
    wlan_network();
#endif

    pmu_set_broadcast_awake(ENABLE);    //set the broadcast awake fun enable
    pmu_set_broadcast_awake_port(UDP_SERVER_PORT);  //set the port of server
    pmu_sysactive_timer_init();
    pmu_set_sysactive_time(10000);
    pmu_release_wakelock(PMU_OS);
    //set tickless sleep type to SLEEP_CG
    pmu_set_sleep_type(SLEEP_CG);

    example_broadcast_wakeup();

#if defined(CONFIG_KERNEL) && !TASK_SCHEDULER_DISABLED
    #ifdef PLATFORM_FREERTOS
    vTaskStartScheduler();
    #endif
#else
    #error !!!Need FREERTOS!!!
#endif

}

