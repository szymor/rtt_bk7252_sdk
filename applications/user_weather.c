/*
 * File      : httpclient.c
 *
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date             Author      Notes
 * 2018-07-20     flybreak     first version
 * 2018-09-05     flybreak     Upgrade API to webclient latest version
 */
#include <webclient.h>  /* 使用 HTTP 协议与服务器通信需要包含此头文件 */
#include <sys/socket.h> /* 使用BSD socket，需要包含socket.h头文件 */
#include <netdb.h>
#include <cJSON.h>
#include <finsh.h>
#include "user_include.h"
#include <time.h>
#include <stdint.h>
#include "ntp.h"    

#define GET_HEADER_BUFSZ        4096        //头部大小
#define GET_RESP_BUFSZ          6 * 1024        //响应缓冲区大小
#define GET_URL_LEN_MAX         256         //网址最大长度
#define GET_URI                 "http://t.weather.sojson.com/api/weather/city/101010100" //获取天气的 API

#define POST_DATA_WEATHER    "{\"weather\":\"%s\"}"


// http://t.weather.sojson.com/api/weather/city/101010100
// {"resultcode":"200","reason":"successed!","result":{"sk":{"temp":"-4","wind_direction":"南风","wind_strength":"1级","humidity":"90%","time":"01:34"},"today":{"temperature":"-4℃~9℃","weather":"雾转晴","weather_id":{"fa":"18","fb":"00"},"wind":"西北风4-5级","week":"星期二","city":"北京","date_y":"2019年12月10日","dressing_index":"较冷","dressing_advice":"建议着厚外套加毛衣等服装。年老体弱者宜着大衣、呢外套加羊毛衫。","uv_index":"中等","comfort_index":"","wash_index":"较不宜","travel_index":"较不宜","exercise_index":"较不宜","drying_index":""},"future":{"day_20191210":{"temperature":"-4℃~9℃","weather":"雾转晴","weather_id":{"fa":"18","fb":"00"},"wind":"西北风4-5级","week":"星期二","date":"20191210"},"day_20191211":{"temperature":"-6℃~4℃","weather":"晴","weather_id":{"fa":"00","fb":"00"},"wind":"西北风3-5级","week":"星期三","date":"20191211"},"day_20191212":{"temperature":"-5℃~5℃","weather":"晴","weather_id":{"fa":"00","fb":"00"},"wind":"西南风微风","week":"星期四","date":"20191212"},"day_20191213":{"temperature":"-3℃~9℃","weather":"晴","weather_id":{"fa":"00","fb":"00"},"wind":"北风3-5级","week":"星期五","date":"20191213"},"day_20191214":{"temperature":"-5℃~6℃","weather":"晴转多云","weather_id":{"fa":"00","fb":"01"},"wind":"南风微风","week":"星期六","date":"20191214"},"day_20191215":{"temperature":"-5℃~5℃","weather":"晴","weather_id":{"fa":"00","fb":"00"},"wind":"西南风微风","week":"星期日","date":"20191215"},"day_20191216":{"temperature":"-3℃~9℃","weather":"晴","weather_id":{"fa":"00","fb":"00"},"wind":"北风3-5级","week":"星期一","date":"20191216"}}},"error_code":0}

int user_weather_data_report(char *data)
{
    char send_data[256] = { 0x00 };

    rt_sprintf(send_data, POST_DATA_WEATHER, data);
    rt_kprintf("send_data is %s\r\n", send_data);
    // send_mq_msg("$dp", send_data, strlen(send_data));
}


/* 天气数据解析 */
void weather_data_parse(rt_uint8_t *data)
{
    int i = 0, size = 0;
    cJSON *root = RT_NULL, *object = RT_NULL, *item_array = RT_NULL, *item = RT_NULL, *pvalue = RT_NULL;
    char *token;
   

    root = cJSON_Parse((const char *)data);
    if (!root)
    {
        rt_kprintf("No memory for cJSON root!\n");
        return;
    }
    object = cJSON_GetObjectItem(root, "data");
    pvalue =cJSON_GetObjectItem(object, "wendu");
    rt_kprintf("\r\ntemp:%s ", pvalue->valuestring);
    user_get_connect_status()->user_data.temp = atoi(pvalue->valuestring);
    pvalue =cJSON_GetObjectItem(object, "shidu");
    for (i = 0; i < strlen(pvalue->valuestring); i++)
    {
        if ('%' == pvalue->valuestring[i])
        {
            pvalue->valuestring[i] = 0x00;
        }
    }
    rt_kprintf("\r\nhumi:%s ", pvalue->valuestring);
    user_get_connect_status()->user_data.humi = atoi(pvalue->valuestring);

    item_array = cJSON_GetObjectItem(object, "forecast");
    size = cJSON_GetArraySize(item_array);
    for (i = 0; i < 1; i ++)
    {
        item =cJSON_GetArrayItem(item_array, i);
        pvalue =cJSON_GetObjectItem(item, "ymd");
        rt_kprintf("\r\nyear-month-day:%s ", pvalue->valuestring);
        pvalue =cJSON_GetObjectItem(item, "type");
        rt_kprintf("\r\nweather:%s", pvalue->valuestring);
        if (!strcmp(pvalue->valuestring, "晴"))
        {
            user_get_connect_status()->user_data.weather = eWeather_SUNNY;
        }else if (!strcmp(pvalue->valuestring, "多云"))
        {
            user_get_connect_status()->user_data.weather = eWeather_CLOUD;
        }else if (!strcmp(pvalue->valuestring, "阴"))
        {
            user_get_connect_status()->user_data.weather = eWeather_SHADE;
        }else if (!strcmp(pvalue->valuestring, "雷阵雨"))
        {
            user_get_connect_status()->user_data.weather = eWeather_THUNDER_RAIN;
        }else if (!strcmp(pvalue->valuestring, "小雨"))
        {
            user_get_connect_status()->user_data.weather = eWeather_LIGHT_RAIN;
        }else if (!strcmp(pvalue->valuestring, "中雨"))
        {
            user_get_connect_status()->user_data.weather = eWeather_MODERATE_RAIN;
        }else if (!strcmp(pvalue->valuestring, "大雨"))
        {
            user_get_connect_status()->user_data.weather = eWeather_HEAVY_RAIN;
        }
        pvalue =cJSON_GetObjectItem(item, "notice");
        rt_kprintf("\r\nnotice:%s", pvalue->valuestring);
        pvalue =cJSON_GetObjectItem(item, "high");
        rt_kprintf("\r\nhigh:%s", pvalue->valuestring);
        pvalue =cJSON_GetObjectItem(item, "low");
        rt_kprintf("\r\nlow:%s\r\n", pvalue->valuestring);
    }

    // user_weather_data_report(pvalue->valuestring);
    user_get_connect_status()->report_status = RT_TRUE;
    

    if (root != RT_NULL)
        cJSON_Delete(root);
}

void weather(void)
{
    rt_uint8_t *buffer = RT_NULL;
    int resp_status;
    struct webclient_session *session = RT_NULL;
    char *weather_url = RT_NULL;
    int content_length = -1, bytes_read = 0;
    int content_pos = 0;
    char *getdata_pos = RT_NULL;
    /* 为 weather_url 分配空间 */
    weather_url = rt_calloc(1, GET_URL_LEN_MAX);
    if (weather_url == RT_NULL)
    {
        rt_kprintf("No memory for weather_url!\n");
        goto __exit;
    }
    /* 拼接 GET 网址 */
    rt_snprintf(weather_url, GET_URL_LEN_MAX, "%s", GET_URI);
    /* 创建会话并且设置响应的大小 */
    session = webclient_session_create(GET_HEADER_BUFSZ);
    if (session == RT_NULL)
    {
        rt_kprintf("No memory for get header!\n");
        goto __exit;
    }
    /* 发送 GET 请求使用默认的头部 */
    if ((resp_status = webclient_get(session, weather_url)) != 200)
    {
        rt_kprintf("webclient GET request failed, response(%d) error.\n", resp_status);
        goto __exit;
    }
    /* 分配用于存放接收数据的缓冲 */
    buffer = rt_calloc(1, GET_RESP_BUFSZ);
    if (buffer == RT_NULL)
    {
        rt_kprintf("No memory for data receive buffer!\n");
        goto __exit;
    }
    content_length = webclient_content_length_get(session);
    // rt_kprintf("recv json data, which's length is %d\r\n", content_length);
    if (content_length < 0)
    {
        getdata_pos = buffer;
        /* 返回的数据是分块传输的. */
        do
        {
            bytes_read = webclient_read(session, getdata_pos, GET_RESP_BUFSZ);
            // rt_kprintf("recv bytes_read is %d\r\n", bytes_read);
            if (bytes_read <= 0)
            {
                break;
            }
            // rt_kprintf("recv data is :%s\r\n", buffer);
            getdata_pos += bytes_read;
        }while (1);
    }
    else
    {
        do
        {
            bytes_read = webclient_read(session, buffer,
                                        content_length - content_pos > GET_RESP_BUFSZ ?
                                        GET_RESP_BUFSZ : content_length - content_pos);
            if (bytes_read <= 0)
            {
                break;
            }
            content_pos += bytes_read;
            // rt_kprintf("bytes_read is %d,  content_pos is %d\r\n", bytes_read, content_pos);
        }while (content_pos < content_length);
    }
    /* 天气数据解析 */
    weather_data_parse(buffer);
__exit:
    /* 释放网址空间 */
    if (weather_url != RT_NULL)
        rt_free(weather_url);
    /* 关闭会话 */
    if (session != RT_NULL)
        webclient_close(session);
    /* 释放缓冲区空间 */
    if (buffer != RT_NULL)
        rt_free(buffer);
}

// MSH_CMD_EXPORT(weather, Get weather by webclient);


void weather_task_thread(void* arg)
{
    uint8_t a = 0;
    uint8_t *p = &a;
	USER_TIME_S current_time = { 0x00 };

    // user_dev_time_flash_read();
    // while (4 != user_get_connect_status()->connect_status)
    // {
    //     rt_thread_delay(1);
    // }
    
    rt_thread_delay(3);
    rt_kprintf("################################### start to get weather\r\n");
    weather();

	while ( 1 )
	{
		user_get_time(&current_time);
        // if ((0 == current_time.hour)  && (0 == current_time.minute) && (0 == current_time.second) )
        if ( 0 == current_time.second )
        {
            rt_kprintf("########################## sync weather and ntp time #####################\r\n");
            user_sntp_time_synced();
        }

        rt_thread_mdelay(1000);
	}
}

void weather_task_start(void)
{
    rt_thread_t tid;

    // /* create the ambient light data upload thread */
    // tid = rt_thread_create("local_weather",
    //                        weather_task_thread,
    //                        RT_NULL,
    //                        6*1024,
    //                        RT_THREAD_PRIORITY_MAX / 3 - 1,
    //                        5);
    // if (tid)
    // {
    //     rt_thread_startup(tid);
    // }
    weather_task_thread(0);
}

