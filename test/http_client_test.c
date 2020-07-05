#include <rtthread.h>
#include <webclient.h>

#define DEBUG_PRINTF     rt_kprintf("[http_client] ");rt_kprintf
#define HTTP_GET_URL "http://v.juhe.cn/weather/index?cityname=%E5%8C%97%E4%BA%AC&dtype=json&format=1&key=7022b77eecfce961ebf58268a656412e"
#define HTTP_POST_URL "http://www.rt-thread.com/service/echo"

static const char *post_data = "RT-Thread is an open source IoT operating system from China!";

/* HTTP client download data by Get request */
static int webclient_get_data(int argc,char *argv[])
{
    unsigned char *buffer = RT_NULL;
    int length = 0;
    int res_len = 0;

    length = webclient_request(HTTP_GET_URL, RT_NULL, 0, RT_NULL, &buffer, &res_len);

    if (length < 0)
    {
        DEBUG_PRINTF("webclient GET request response data error.\r\n");
        return -RT_ERROR;
    }

    DEBUG_PRINTF("webclient GET request response data :\r\n");
    DEBUG_PRINTF("%s\r\n",buffer);

    web_free(buffer);
    return RT_EOK;
}

/* HTTP client upload data to server by POST request */
static int webclient_post_data(int argc,char *argv[])
{
    unsigned char *buffer = RT_NULL;
    int length = 0;
    int res_len = 0;

    length = webclient_request(HTTP_POST_URL, RT_NULL, post_data, strlen(post_data), &buffer, &res_len);
    if (length < 0)
    {
        DEBUG_PRINTF("webclient POST request response data error.\r\n");
        return -RT_ERROR;
    }

    DEBUG_PRINTF("webclient POST request response data :\r\n");
    DEBUG_PRINTF("%s\r\n",buffer);

    web_free(buffer);
    return RT_EOK;
}

MSH_CMD_EXPORT(webclient_get_data, webclient_get_data);
MSH_CMD_EXPORT(webclient_post_data,webclient_post_data);

