/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "lite-utils.h"
#include "qcloud_utils_timer.h"

#include "rtthread.h"
#include "wlan_dev.h"

#define DATA_TEMPLATE_CLOCK_THREAD_STACK_SIZE 1024 * 8
#define YEILD_THREAD_STACK_SIZE     4096

static uint8_t clock_running_state = 0;
#ifdef AUTH_MODE_CERT
static char sg_cert_file[PATH_MAX + 1];      // full path of device cert file
static char sg_key_file[PATH_MAX + 1];       // full path of device key file
#endif

static DeviceInfo clock_devInfo;
static Timer clock_reportTimer;


static MQTTEventType sg_subscribe_event_result = MQTT_EVENT_UNDEF;
static bool clock_control_msg_arrived = false;
static char clock_data_report_buffer[2048];
static size_t clock_data_report_buffersize = sizeof(clock_data_report_buffer) / sizeof(clock_data_report_buffer[0]);


/*data_config.c can be generated by tools/codegen.py -c xx/product.json*/
/*-----------------data config start  -------------------*/

#define CLOCK_TOTAL_PROPERTY_COUNT     4
#define CLOCK_MAX_STR_TIMER_SET_LEN    (64)
#define CLOCK_MAX_STR_WEATHER_LEN      (64)
#define CLOCK_MAX_STR_NAME_LEN         (64)

static sDataPoint    clock_DataTemplate[CLOCK_TOTAL_PROPERTY_COUNT];

typedef enum {
    eWeather_SUNNY = 0,
    eWeather_LIGHT_RAIN = 1,
    eWeather_HEAVY_RAIN = 2,
} eWeather;

typedef struct _ClockProductDataDefine {
    TYPE_DEF_TEMPLATE_BOOL m_voice;
    TYPE_DEF_TEMPLATE_INT  m_temp;
    TYPE_DEF_TEMPLATE_INT  m_humi;
    TYPE_DEF_TEMPLATE_ENUM m_weather;
} ClockProductDataDefine;

static   ClockProductDataDefine     clock_ProductData;

static void _init_data_template(void)
{
    memset((void *) & clock_ProductData, 0, sizeof(ClockProductDataDefine));

    clock_ProductData.m_voice = 0;
    clock_DataTemplate[0].data_property.key  = "voice";
    clock_DataTemplate[0].data_property.data = &clock_ProductData.m_voice;
    clock_DataTemplate[0].data_property.type = TYPE_TEMPLATE_BOOL;

    clock_ProductData.m_temp = 0;
    clock_DataTemplate[1].data_property.key  = "temp";
    clock_DataTemplate[1].data_property.data = &clock_ProductData.m_temp;
    clock_DataTemplate[1].data_property.type = TYPE_TEMPLATE_INT;

    clock_ProductData.m_humi = 0;
    clock_DataTemplate[2].data_property.key  = "humi";
    clock_DataTemplate[2].data_property.data = &clock_ProductData.m_humi;
    clock_DataTemplate[2].data_property.type = TYPE_TEMPLATE_INT;

    clock_ProductData.m_weather = eWeather_SUNNY;
    clock_DataTemplate[3].data_property.key  = "weather";
    clock_DataTemplate[3].data_property.data = &clock_ProductData.m_weather;
    clock_DataTemplate[3].data_property.type = TYPE_TEMPLATE_ENUM;
};
/*-----------------data config end  -------------------*/


/*event_config.c can be generated by tools/codegen.py -c xx/product.json*/
/*-----------------event config start  -------------------*/
#ifdef EVENT_POST_ENABLED
#define EVENT_COUNTS     (3)
#define MAX_EVENT_STR_MESSAGE_LEN (64)
#define MAX_EVENT_STR_NAME_LEN (64)


static TYPE_DEF_TEMPLATE_BOOL sg_status;
static TYPE_DEF_TEMPLATE_STRING sg_message[MAX_EVENT_STR_MESSAGE_LEN + 1];
static DeviceProperty g_propertyEvent_status_report[] = {

    {.key = "status", .data = &sg_status, .type = TYPE_TEMPLATE_BOOL},
    {.key = "message", .data = sg_message, .type = TYPE_TEMPLATE_STRING},
};

static TYPE_DEF_TEMPLATE_FLOAT sg_voltage;
static DeviceProperty g_propertyEvent_low_voltage[] = {

    {.key = "voltage", .data = &sg_voltage, .type = TYPE_TEMPLATE_FLOAT},
};

static TYPE_DEF_TEMPLATE_STRING sg_name[MAX_EVENT_STR_NAME_LEN + 1];
static TYPE_DEF_TEMPLATE_INT sg_error_code;
static DeviceProperty g_propertyEvent_hardware_fault[] = {

    {.key = "name", .data = sg_name, .type = TYPE_TEMPLATE_STRING},
    {.key = "error_code", .data = &sg_error_code, .type = TYPE_TEMPLATE_INT},
};


static sEvent g_events[] = {

    {
        .event_name = "status_report",
        .type = "info",
        .timestamp = 0,
        .eventDataNum = sizeof(g_propertyEvent_status_report) / sizeof(g_propertyEvent_status_report[0]),
        .pEventData = g_propertyEvent_status_report,
    },
    {
        .event_name = "low_voltage",
        .type = "alert",
        .timestamp = 0,
        .eventDataNum = sizeof(g_propertyEvent_low_voltage) / sizeof(g_propertyEvent_low_voltage[0]),
        .pEventData = g_propertyEvent_low_voltage,
    },
    {
        .event_name = "hardware_fault",
        .type = "fault",
        .timestamp = 0,
        .eventDataNum = sizeof(g_propertyEvent_hardware_fault) / sizeof(g_propertyEvent_hardware_fault[0]),
        .pEventData = g_propertyEvent_hardware_fault,
    },
};

/*-----------------event config end -------------------*/


static void update_events_timestamp(sEvent *pEvents, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (NULL == (&pEvents[i])) {
            Log_e("null event pointer");
            return;
        }
#ifdef EVENT_TIMESTAMP_USED
        pEvents[i].timestamp = HAL_Timer_current_sec(); //should be UTC and accurate
#else
        pEvents[i].timestamp = 0;
#endif
    }
}

static void event_post_cb(void *pClient, MQTTMessage *msg)
{
    Log_d("recv event reply, clear event");
//    IOT_Event_clearFlag(pClient, FLAG_EVENT0);
}

//event check and post
static void eventPostCheck(void *client)
{
    int i;
    int rc;
    uint32_t eflag;
    uint8_t EventCont;
    sEvent *pEventList[EVENT_COUNTS];


    eflag = IOT_Event_getFlag(client);
    if ((EVENT_COUNTS > 0 ) && (eflag > 0)) {
        EventCont = 0;
        for (i = 0; i < EVENT_COUNTS; i++) {
            if ((eflag & (1 << i))&ALL_EVENTS_MASK) {
                pEventList[EventCont++] = &(g_events[i]);
                update_events_timestamp(&g_events[i], 1);
                IOT_Event_clearFlag(client, (1 << i)&ALL_EVENTS_MASK);
            }
        }

        rc = IOT_Post_Event(client, clock_data_report_buffer, clock_data_report_buffersize, \
                            EventCont, pEventList, event_post_cb);
        if (rc < 0) {
            Log_e("event post failed: %d", rc);
        }
    }
}

#endif

/*action_config.c can be generated by tools/codegen.py -c xx/product.json*/
/*-----------------action config start  -------------------*/
#ifdef ACTION_ENABLED

#define TOTAL_ACTION_COUNTS     (1)

static TYPE_DEF_TEMPLATE_INT sg_blink_in_period = 5;
static DeviceProperty g_actionInput_blink[] = {
    {.key = "period", .data = &sg_blink_in_period, .type = TYPE_TEMPLATE_INT}
};
static TYPE_DEF_TEMPLATE_BOOL sg_blink_out_result = 0;
static DeviceProperty g_actionOutput_blink[] = {

    {.key = "result", .data = &sg_blink_out_result, .type = TYPE_TEMPLATE_BOOL},
};

static DeviceAction g_actions[] = {

    {
        .pActionId = "blink",
        .timestamp = 0,
        .input_num = sizeof(g_actionInput_blink) / sizeof(g_actionInput_blink[0]),
        .output_num = sizeof(g_actionOutput_blink) / sizeof(g_actionOutput_blink[0]),
        .pInput = g_actionInput_blink,
        .pOutput = g_actionOutput_blink,
    },
};
/*-----------------action config end    -------------------*/
static void OnActionCallback(void *pClient, const char *pClientToken, DeviceAction *pAction)
{
    int i;
    sReplyPara replyPara;

    //control clock blink
    int period = 0;
    DeviceProperty *pActionInput = pAction->pInput;
    for (i = 0; i < pAction->input_num; i++) {
        if (!strcmp(pActionInput[i].key, "period")) {
            period = *((int*)pActionInput[i].data);
        } else {
            Log_e("no such input[%s]!", pActionInput[i].key);
        }
    }

    //do blink
    HAL_Printf( "%s[clocking blink][****]" ANSI_COLOR_RESET, ANSI_COLOR_RED);
    HAL_SleepMs(period * 1000);
    HAL_Printf( "\r%s[clocking blink][****]" ANSI_COLOR_RESET, ANSI_COLOR_GREEN);
    HAL_SleepMs(period * 1000);
    HAL_Printf( "\r%s[clocking blink][****]\n" ANSI_COLOR_RESET, ANSI_COLOR_RED);

    // construct output
    memset((char *)&replyPara, 0, sizeof(sReplyPara));
    replyPara.code = eDEAL_SUCCESS;
    replyPara.timeout_ms = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    strcpy(replyPara.status_msg, "action execute success!"); //add the message about the action resault

    DeviceProperty *pActionOutnput = pAction->pOutput;
    *(int*)(pActionOutnput[0].data) = 0; //set result

    IOT_ACTION_REPLY(pClient, pClientToken, clock_data_report_buffer, clock_data_report_buffersize, pAction, &replyPara);
}

static int _register_data_template_action(void *pTemplate_client)
{
    int i, rc;

    for (i = 0; i < TOTAL_ACTION_COUNTS; i++) {
        rc = IOT_Template_Register_Action(pTemplate_client, &g_actions[i], OnActionCallback);
        if (rc != QCLOUD_RET_SUCCESS) {
            rc = IOT_Template_Destroy(pTemplate_client);
            Log_e("register device data template action failed, err: %d", rc);
            return rc;
        } else {
            Log_i("data template action=%s registered.", g_actions[i].pActionId);
        }
    }

    return QCLOUD_RET_SUCCESS;
}
#endif

static void clock_event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{
    uintptr_t packet_id = (uintptr_t)msg->msg;

    switch (msg->event_type) {
        case MQTT_EVENT_UNDEF:
            Log_i("undefined event occur.");
            break;

        case MQTT_EVENT_DISCONNECT:
            Log_i("MQTT disconnect.");
            break;

        case MQTT_EVENT_RECONNECT:
            Log_i("MQTT reconnect.");
            break;

        case MQTT_EVENT_SUBCRIBE_SUCCESS:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_SUBCRIBE_TIMEOUT:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_SUBCRIBE_NACK:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_SUCCESS:
            Log_i("publish success, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_TIMEOUT:
            Log_i("publish timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_NACK:
            Log_i("publish nack, packet-id=%u", (unsigned int)packet_id);
            break;
        default:
            Log_i("Should NOT arrive here.");
            break;
    }
}

/*add user init code, like sensor init*/
static void clock_usr_init(void)
{
    Log_d("add your init code here");
}

// Setup MQTT construct parameters
static int clock_setup_connect_init_params(TemplateInitParams* initParams)
{
    int ret;

    ret = HAL_GetDevInfo((void *)&clock_devInfo);
    if (QCLOUD_RET_SUCCESS != ret) {
        return ret;
    }

    initParams->device_name = clock_devInfo.device_name;
    initParams->product_id = clock_devInfo.product_id;

#ifdef AUTH_MODE_CERT
    /* TLS with certs*/
    char certs_dir[PATH_MAX + 1] = "certs";
    char current_path[PATH_MAX + 1];
    char *cwd = getcwd(current_path, sizeof(current_path));
    if (cwd == NULL) {
        Log_e("getcwd return NULL");
        return QCLOUD_ERR_FAILURE;
    }
    sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, clock_devInfo.dev_cert_file_name);
    sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, clock_devInfo.dev_key_file_name);

    initParams->cert_file = sg_cert_file;
    initParams->key_file = sg_key_file;
#else
    initParams->device_secret = clock_devInfo.device_secret;
#endif

    initParams->command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;
    initParams->auto_connect_enable = 1;
    initParams->event_handle.h_fp = clock_event_handler;

    return QCLOUD_RET_SUCCESS;
}

/*control msg from server will trigger this callback*/
static void ClockOnControlMsgCallback(void *pClient, const char *pJsonValueBuffer, uint32_t valueLength, DeviceProperty *pProperty)
{
    int i = 0;

    for (i = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
        /* handle self defined string/json here. Other properties are dealed in _handle_delta()*/
        if (strcmp(clock_DataTemplate[i].data_property.key, pProperty->key) == 0) {
            clock_DataTemplate[i].state = eCHANGED;
            Log_i("Property=%s changed", pProperty->key);
            clock_control_msg_arrived = true;
            return;
        }
    }

    Log_e("Property=%s changed no match", pProperty->key);
}

static void ClockOnReportReplyCallback(void *pClient, Method method, ReplyAck replyAck, const char *pJsonDocument, void *pUserdata)
{
    Log_i("recv report_reply(ack=%d): %s", replyAck, pJsonDocument);
}


// register data template properties
static int clock_register_data_template_property(void *pTemplate_client)
{
    int i, rc;

    for (i = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
        rc = IOT_Template_Register_Property(pTemplate_client, &clock_DataTemplate[i].data_property, ClockOnControlMsgCallback);
        if (rc != QCLOUD_RET_SUCCESS) {
            rc = IOT_Template_Destroy(pTemplate_client);
            Log_e("register device data template property failed, err: %d", rc);
            return rc;
        } else {
            Log_i("data template property=%s registered.", clock_DataTemplate[i].data_property.key);
        }
    }

    return QCLOUD_RET_SUCCESS;
}

/*get property state, changed or not*/
static eDataState  clock_get_property_state(void *pProperyData)
{
    int i;

    for (i = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
        if (clock_DataTemplate[i].data_property.data == pProperyData) {
            return clock_DataTemplate[i].state;
        }
    }

    Log_e("no property matched");
    return eNOCHANGE;
}


/*set property state, changed or no change*/
static void  clock_set_propery_state(void *pProperyData, eDataState state)
{
    int i;

    for (i = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
        if (clock_DataTemplate[i].data_property.data == pProperyData) {
            clock_DataTemplate[i].state = state;
            break;
        }
    }
}


/* demo for clock logic deal */
static void clock_deal_down_stream_user_logic(void *client, ClockProductDataDefine *clock)
{
    int i;
    const char * ansi_weather_name = NULL;

    /* clock color */
    switch (clock->m_weather) {
        case eWeather_SUNNY:
            ansi_weather_name = " SUNNY ";
            break;
        case eWeather_LIGHT_RAIN:
            ansi_weather_name = "LIGHT RAIN";
            break;
        case eWeather_HEAVY_RAIN:
            ansi_weather_name = " HEAVY RAIN";
            break;
        default:
            ansi_weather_name = "UNKNOWN";
            break;
    }
    

    if (clock->m_voice) {
        /* clock is on , show with the properties*/
        HAL_Printf( "[  voice is on  ]|[temp:%d]|[humi:%d]|[weather:%s]\n", \
                    clock->m_temp, clock->m_humi, ansi_weather_name);
    } else {
        /* clock is off */
        HAL_Printf( "[  voice is off ]|[temp:%d]|[humi:%d]|[weather:%s]\n", \
                    clock->m_temp, clock->m_humi, ansi_weather_name);
    }

    if (eCHANGED == clock_get_property_state(&clock->m_voice)) {
#ifdef EVENT_POST_ENABLED
        if (clock->m_voice) {
            *(TYPE_DEF_TEMPLATE_BOOL *)g_events[0].pEventData[0].data = 1;
            memset((TYPE_DEF_TEMPLATE_STRING *)g_events[0].pEventData[1].data, 0, MAX_EVENT_STR_MESSAGE_LEN);
            strcpy((TYPE_DEF_TEMPLATE_STRING *)g_events[0].pEventData[1].data, "clock on");
        } else {
            *(TYPE_DEF_TEMPLATE_BOOL *)g_events[0].pEventData[0].data = 0;
            memset((TYPE_DEF_TEMPLATE_STRING *)g_events[0].pEventData[1].data, 0, MAX_EVENT_STR_MESSAGE_LEN);
            strcpy((TYPE_DEF_TEMPLATE_STRING *)g_events[0].pEventData[1].data, "clock off");
        }

        //switch state changed set EVENT0 flag, the events will be posted by eventPostCheck
        IOT_Event_setFlag(client, FLAG_EVENT0);
#else
        Log_d("clock voice mode state changed");
#endif
    }
}

/*example for cycle report, you can delete this for your needs*/
static void clock_cycle_report(Timer *reportTimer)
{
    int i;

    if (expired(reportTimer)) {
        for (i = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
            clock_set_propery_state(clock_DataTemplate[i].data_property.data, eCHANGED);
            countdown_ms(reportTimer, 5000);
        }
    }
}

/*get local property data, like sensor data*/
static void clock_refresh_local_property(void)
{
    //add your local property refresh logic, cycle report for example
    clock_cycle_report(&clock_reportTimer);
}

/*find propery need report*/
static int clock_find_wait_report_property(DeviceProperty *pReportDataList[])
{
    int i, j;

    for (i = 0, j = 0; i < CLOCK_TOTAL_PROPERTY_COUNT; i++) {
        if (eCHANGED == clock_DataTemplate[i].state) {
            pReportDataList[j++] = &(clock_DataTemplate[i].data_property);
            clock_DataTemplate[i].state = eNOCHANGE;
        }
    }

    return j;
}

/* demo for up-stream code */
static int clock_deal_up_stream_user_logic(DeviceProperty *pReportDataList[], int *pCount)
{
    //refresh local property
    clock_refresh_local_property();

    /*find propery need report*/
    *pCount = clock_find_wait_report_property(pReportDataList);

    return (*pCount > 0) ? QCLOUD_RET_SUCCESS : QCLOUD_ERR_FAILURE;
}

/*You should get the real info for your device, here just for example*/
static int clock_get_sys_info(void *handle, char *pJsonDoc, size_t sizeOfBuffer)
{
    /*platform info has at least one of module_hardinfo/module_softinfo/fw_ver property*/
    DeviceProperty plat_info[] = {
        {.key = "module_hardinfo", .type = TYPE_TEMPLATE_STRING, .data = "ESP8266"},
        {.key = "module_softinfo", .type = TYPE_TEMPLATE_STRING, .data = "V1.0"},
        {.key = "fw_ver",          .type = TYPE_TEMPLATE_STRING, .data = QCLOUD_IOT_DEVICE_SDK_VERSION},
        {.key = "imei",            .type = TYPE_TEMPLATE_STRING, .data = "11-22-33-44"},
        {.key = "lat",             .type = TYPE_TEMPLATE_STRING, .data = "22.546015"},
        {.key = "lon",             .type = TYPE_TEMPLATE_STRING, .data = "113.941125"},
        {NULL, NULL, 0}  //end
    };

    /*self define info*/
    DeviceProperty self_info[] = {
        {.key = "append_info", .type = TYPE_TEMPLATE_STRING, .data = "your self define info"},
        {NULL, NULL, 0}  //end
    };

    return IOT_Template_JSON_ConstructSysInfo(handle, pJsonDoc, sizeOfBuffer, plat_info, self_info);
}

static rt_sem_t wait_sem = RT_NULL;
static struct rt_wlan_info wlan_info;

static void wifi_connect_callback(struct rt_wlan_device *device, rt_wlan_event_t event, void *user_data)
{
    HAL_Printf("wifi_ocnnect_callback.\r\n");
    rt_sem_release(wait_sem);
}
static int data_template_clock_thread(void)
{

    DeviceProperty *pReportDataList[CLOCK_TOTAL_PROPERTY_COUNT];
    sReplyPara replyPara;
    int ReportCont;
    int rc, ret;
    struct rt_wlan_device *wlan;

    //init log level
    IOT_Log_Set_Level(eLOG_DEBUG);

    // wait for connect to router......
    wait_sem = rt_sem_create("sem_conn", 0, RT_IPC_FLAG_FIFO);

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
    /* TODO: use easy-join to replace */
    rt_wlan_info_init(&wlan_info, WIFI_STATION, SECURITY_WPA2_MIXED_PSK, "lxy2305");
    rt_wlan_connect(wlan, &wlan_info, "yjplhb123456");
    rt_wlan_info_deinit(&wlan_info);

    rt_kprintf("start to connect ap ...\n");

    // wait until module connect to ap success
    ret = rt_sem_take(wait_sem, RT_WAITING_FOREVER);
    if (0 != ret)
    {
        rt_kprintf("wait_sem error!\r\n");
    }
    rt_kprintf("connect to ap success!\r\n");

    rt_thread_mdelay(3000);

    //init connection
    TemplateInitParams init_params = DEFAULT_TEMPLATE_INIT_PARAMS;
    rc = clock_setup_connect_init_params(&init_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("init params err,rc=%d", rc);
        return rc;
    }

    void *client = IOT_Template_Construct(&init_params, NULL);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        Log_e("Cloud Device Construct Failed");
        return QCLOUD_ERR_FAILURE;
    }

#ifdef MULTITHREAD_ENABLED
    if (QCLOUD_RET_SUCCESS != IOT_Template_Start_Yield_Thread(client)) {
        Log_e("start template yield thread fail");
        goto exit;
    }
#endif

    //usr init
    clock_usr_init();

    //init data template
    _init_data_template();

    //register data template propertys here
    rc = clock_register_data_template_property(client);
    if (rc == QCLOUD_RET_SUCCESS) {
        Log_i("Register data template propertys Success");
    } else {
        Log_e("Register data template propertys Failed: %d", rc);
        goto exit;
    }

    //register data template actions here
#ifdef ACTION_ENABLED
    rc = _register_data_template_action(client);
    if (rc == QCLOUD_RET_SUCCESS) {
        Log_i("Register data template actions Success");
    } else {
        Log_e("Register data template actions Failed: %d", rc);
        goto exit;
    }
#endif

    //report device info, then you can manager your product by these info, like position
    rc = clock_get_sys_info(client, clock_data_report_buffer, clock_data_report_buffersize);
    if (QCLOUD_RET_SUCCESS == rc) {
        rc = IOT_Template_Report_SysInfo_Sync(client, clock_data_report_buffer, clock_data_report_buffersize, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
        if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("Report system info fail, err: %d", rc);
        }
    } else {
        Log_e("Get system info fail, err: %d", rc);
    }

    //get the property changed during offline
    rc = IOT_Template_GetStatus_sync(client, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("Get data status fail, err: %d", rc);
    } else {
        Log_d("Get data status success");
    }

    //init a timer for cycle report, you could delete it or not for your needs
    InitTimer(&clock_reportTimer);
    clock_running_state = 1;

    while (IOT_Template_IsConnected(client) || rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT
           || rc == QCLOUD_RET_MQTT_RECONNECTED || QCLOUD_RET_SUCCESS == rc) {

        if(0 == clock_running_state) {
            break;
        }

#ifndef MULTITHREAD_ENABLED
        rc = IOT_Template_Yield(client, 200);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            HAL_SleepMs(1000);
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("Exit loop caused of errCode: %d", rc);
        }
#endif

        /* handle control msg from server */
        if (clock_control_msg_arrived) {
            Log_d("get control msg data from server.\r\n");
            clock_deal_down_stream_user_logic(client, &clock_ProductData);
            /* control msg should reply, otherwise server treat device didn't receive and retain the msg which would be get by  get status*/
            memset((char *)&replyPara, 0, sizeof(sReplyPara));
            replyPara.code = eDEAL_SUCCESS;
            replyPara.timeout_ms = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
            replyPara.status_msg[0] = '\0';         //add extra info to replyPara.status_msg when error occured

            rc = IOT_Template_ControlReply(client, clock_data_report_buffer, clock_data_report_buffersize, &replyPara);
            if (rc == QCLOUD_RET_SUCCESS) {
                Log_d("Contol msg reply success");
                clock_control_msg_arrived = false;
            } else {
                Log_e("Contol msg reply failed, err: %d", rc);
            }
        }

        /*report msg to server*/
        /*report the lastest properties's status*/
        if (QCLOUD_RET_SUCCESS == clock_deal_up_stream_user_logic(pReportDataList, &ReportCont)) {

            rc = IOT_Template_JSON_ConstructReportArray(client, clock_data_report_buffer, clock_data_report_buffersize, ReportCont, pReportDataList);
            if (rc == QCLOUD_RET_SUCCESS) {
                rc = IOT_Template_Report(client, clock_data_report_buffer, clock_data_report_buffersize,
                                         ClockOnReportReplyCallback, NULL, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
                if (rc == QCLOUD_RET_SUCCESS) {

                    Log_i("data template reporte success");
                } else {
                    Log_e("data template reporte failed, err: %d", rc);
                }
            } else {
                Log_e("construct reporte data failed, err: %d", rc);
            }

        }

#ifdef EVENT_POST_ENABLED
        eventPostCheck(client);
#endif

        HAL_SleepMs(1000);
    }

exit:

#ifdef MULTITHREAD_ENABLED
    IOT_Template_Stop_Yield_Thread(client);
#endif
    rc = IOT_Template_Destroy(client);
    clock_running_state = 0;

    return rc;
}

int clock_main( void )
{
    rt_thread_t tid;
    int stack_size = DATA_TEMPLATE_CLOCK_THREAD_STACK_SIZE;

    tid = rt_thread_create("data_template_clock", (void (*)(void *))data_template_clock_thread,
                           NULL, stack_size, RT_THREAD_PRIORITY_MAX / 2 - 1, 10);

    if (tid != RT_NULL) {
        rt_thread_startup(tid);
    }

    return 0;
}
