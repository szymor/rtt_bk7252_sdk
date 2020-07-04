#include "user_include.h"
#include <drv_lcd.h>
#include "sensor.h"
#include "sensor_dallas_dht11.h"
#include <time.h>


#define DBG_TAG "bsp"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define PIN_KEY0               13
#define PIN_BEEP               0

/* Modify this pin according to the actual wiring situation */
#define USER_DHT11_DATA_PIN    1  //P1 SDA
#define POST_DATA_TEMP    "{\"temp\":%d,\"temp_mode\":1,\"humi\":%d,\"time_mode\":1,\"time_show_mode\":1,\"voice\":1}"


void beep_ctrl(void *args)
{
    if(rt_pin_read(PIN_KEY0) == PIN_LOW)
    {
        rt_pin_write(PIN_BEEP, PIN_HIGH);
        LOG_D("KEY0 pressed. beep on");
    }
    else
    {
        rt_pin_write(PIN_BEEP, PIN_LOW);  
        LOG_D("beep off");        
    }
}

void beep_init(void)
{
    // /* 设置 KEY0 引脚为输入模式 */
    // rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT);
    // /* KEY0 引脚绑定中断回调函数 */
    // rt_pin_attach_irq(PIN_KEY0, PIN_IRQ_MODE_RISING_FALLING, beep_ctrl, RT_NULL);
    // /* 使能中断 */
    // rt_pin_irq_enable(PIN_KEY0, PIN_IRQ_ENABLE);
    
    // /* 设置 BEEP 引脚为输出模式 */
    // rt_pin_mode(PIN_BEEP, PIN_MODE_OUTPUT);
    // /* 默认蜂鸣器不鸣叫 */
    // rt_pin_write(PIN_BEEP, PIN_LOW);
}

void beep_open(void)
{
    rt_pin_write(PIN_BEEP, PIN_HIGH);
}

void beep_close(void)
{
    rt_pin_write(PIN_BEEP, PIN_LOW);
}

// static int user_dht11_init(void)
// {
//     struct rt_sensor_config cfg;
//     int pin = USER_DHT11_DATA_PIN;

//     cfg.intf.user_data = (void *)pin;
//     rt_hw_dht11_init("dht11", &cfg);

//     return RT_EOK;
// }

int user_data_report(int temp, int humi)
{
    char send_data[256] = { 0x00 };

    rt_sprintf(send_data, POST_DATA_TEMP, temp, humi);
    rt_kprintf("send_data is %s\r\n", send_data);
    // send_mq_msg("$dp", send_data, strlen(send_data));
}

void lcd_task_thread(void* arg)
{
	uint8_t i = 0;
    rt_size_t res;
    rt_device_t dev = RT_NULL;
    struct rt_sensor_data sensor_data = { 0x00 };
    rt_uint8_t get_data_freq = 1; /* 1Hz */
    int value_change_flag = 0;
    char sval[64] = { 0x00 };
    uint8_t temp = 0, temp_old = 0;
    uint8_t humi = 0, humi_old = 0;
    // get current time
    time_t now;
    char month[4] = { 0x00 };
    char week[4] = { 0x00 };
    char day[4] = {0x00 };
    char hour[4] = { 0x00 };
    char minute[4] = {0x00 };
    char second[4] = {0x00 };
    int year[4] = {0x00 };
    int value = 0;

    dht11_init(USER_DHT11_DATA_PIN);

    sensor_data.data.temp = dht11_get_temperature(USER_DHT11_DATA_PIN);
    if (sensor_data.data.temp >= 0)
    {
            temp_old = (sensor_data.data.temp & 0xffff) >> 0;      // get temp
            humi_old = (sensor_data.data.temp & 0xffff0000) >> 16; // get humi
        rt_kprintf("temp:%d, humi:%d\n" ,temp, humi);
    }else
    {
        rt_kprintf("get temperature failed\r\n");
    }

	while ( 1 )
	{
        sensor_data.data.temp = dht11_get_temperature(USER_DHT11_DATA_PIN);
        if (sensor_data.data.temp >= 0)
        {
            temp = (sensor_data.data.temp & 0xffff) >> 0;      // get temp
            humi = (sensor_data.data.temp & 0xffff0000) >> 16; // get humi
            rt_kprintf("temp:%d, humi:%d\r\n" ,temp, humi);
            if (temp != temp_old)
            {
                value_change_flag = 1;
                temp_old = temp;
                lcd_show_num(10+60, 20+24+24, temp_old, 1, 24);
                // lcd_show_string(10+90, 20+24+24, 24, "'C");
            }
            if (humi != humi_old)
            {
                value_change_flag = 1;
                humi_old = humi;
                lcd_show_num(10+60, 20+24+24+24, humi_old, 1, 24);
            }
        }

        if (1 == value_change_flag)
        {
            value_change_flag = 0;
            if (4 == user_get_connect_status()->connect_status)
            {
                user_data_report(temp, humi);
            }
        }
        /* output current time */
        now = time(RT_NULL);
        // rt_kprintf("%s", ctime(&now));  //Mon Dec  9 13:14:55 2019
        sscanf(ctime(&now), "%[^ ] %[^ ] %[^ ] %[^:]:%[^:]:%[^ ] %s", week, month, day, hour, minute, second, year);
        if (NULL != rt_strstr(month, "Jan"))
        {
            value = 1;
        }else if (NULL != rt_strstr(month, "Feb"))
        {
            value = 2;
        }else if (NULL != rt_strstr(month, "Mar"))
        {
            value = 3;
        }else if (NULL != rt_strstr(month, "Apr"))
        {
            value = 4;
        }else if (NULL != rt_strstr(month, "May"))
        {
            value = 5; 
        }else if (NULL != rt_strstr(month, "Jun"))
        {
            value = 6;  
        }else if (NULL != rt_strstr(month, "Jul"))
        {
            value = 7; 
        }else if (NULL != rt_strstr(month, "Aug"))
        {
            value = 8; 
        }else if (NULL != rt_strstr(month, "Sep"))
        {
            value = 9; 
        }else if (NULL != rt_strstr(month, "Oct"))
        {
            value = 10; 
        }else if (NULL != rt_strstr(month, "Nov"))
        {
            value = 11; 
        }else if (NULL != rt_strstr(month, "Dec"))
        {
            value = 12; 
        }
        rt_sprintf(sval, "%s-%d-%s %s:%s:%s", year, value, day, hour, minute, second);
        lcd_show_string(10, 20+24+24+24+24+24, 24, sval);
        rt_thread_mdelay(1000);
	}
}


int bsp_init(void)
{
    rt_thread_t tid;

    beep_init();
    //     /* 清屏 */
    // lcd_clear(WHITE);

    // /* 设置背景色和前景色 */
    // lcd_set_color(WHITE, BLACK);

    // /* 在 LCD 上显示字符 */
    // lcd_show_string(10, 20, 24, "Smart weather clock");
    // lcd_show_string(10, 20+24+24, 24, "Temp:");
    // lcd_show_string(10, 20+24+24+24, 24, "Humi:");
    // // lcd_show_string(10, 20+24+24+24+24, 24, "weather:");
    // lcd_show_string(10, 20+24+24+24+24+24, 24, "2019-12-09 19:32");
    // /* 在 LCD 上画线 */
    // // lcd_draw_line(0, 69+16+24+32, 240, 69+16+24+32);
    
    // /* 在 LCD 上画一个同心圆 */
    // // lcd_draw_point(120, 194);
    // // for (int i = 0; i < 46; i += 4)
    // // {
    // //     lcd_draw_circle(120, 194, i);
    // // }

    /* create the ambient light data upload thread */
    // tid = rt_thread_create("lcd",
    //                        lcd_task_thread,
    //                        RT_NULL,
    //                        2 * 1024,
    //                        RT_THREAD_PRIORITY_MAX / 3 - 1,
    //                        5);
    // if (tid)
    // {
    //     rt_thread_startup(tid);
    // }

}